// Copyright 2016 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "DolphinWX/ISOProperties/FilesystemPanel.h"

#include <array>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <vector>

#include <wx/bitmap.h>
#include <wx/button.h>
#include <wx/filepicker.h>
#include <wx/imaglist.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/progdlg.h>
#include <wx/sizer.h>
#include <wx/textctrl.h>
#include <wx/treectrl.h>

#include "Common/CommonPaths.h"
#include "Common/FileUtil.h"
#include "Common/Logging/Log.h"
#include "DiscIO/Enums.h"
#include "DiscIO/Filesystem.h"
#include "DiscIO/Volume.h"
#include "DolphinWX/ISOFile.h"
#include "DolphinWX/WxUtils.h"

namespace
{
class WiiPartition final : public wxTreeItemData
{
public:
  WiiPartition(std::unique_ptr<DiscIO::FileSystem> filesystem_) : filesystem{std::move(filesystem_)}
  {
  }

  std::unique_ptr<DiscIO::FileSystem> filesystem;
};

enum : int
{
  ICON_DISC,
  ICON_FOLDER,
  ICON_FILE
};

wxImageList* LoadIconBitmaps(const wxWindow* context)
{
  static constexpr std::array<const char*, 3> icon_names{
      {"isoproperties_disc", "isoproperties_folder", "isoproperties_file"}};

  const wxSize icon_size = context->FromDIP(wxSize(16, 16));
  auto* const icon_list = new wxImageList(icon_size.GetWidth(), icon_size.GetHeight());

  for (const auto& name : icon_names)
  {
    icon_list->Add(
        WxUtils::LoadScaledResourceBitmap(name, context, icon_size, wxDefaultSize,
                                          WxUtils::LSI_SCALE_DOWN | WxUtils::LSI_ALIGN_CENTER));
  }

  return icon_list;
}

void CreateDirectoryTree(wxTreeCtrl* tree_ctrl, wxTreeItemId parent,
                         const DiscIO::FileInfo& directory)
{
  for (const DiscIO::FileInfo& file_info : directory)
  {
    const wxString name = StrToWxStr(file_info.GetName());
    if (file_info.IsDirectory())
    {
      wxTreeItemId item = tree_ctrl->AppendItem(parent, name, ICON_FOLDER);
      CreateDirectoryTree(tree_ctrl, item, file_info);
    }
    else
    {
      tree_ctrl->AppendItem(parent, name, ICON_FILE);
    }
  }
}

WiiPartition* FindWiiPartition(wxTreeCtrl* tree_ctrl, const wxString& label)
{
  wxTreeItemIdValue cookie;
  auto partition = tree_ctrl->GetFirstChild(tree_ctrl->GetRootItem(), cookie);

  while (partition.IsOk())
  {
    const wxString partition_label = tree_ctrl->GetItemText(partition);

    if (partition_label == label)
      return static_cast<WiiPartition*>(tree_ctrl->GetItemData(partition));

    partition = tree_ctrl->GetNextSibling(partition);
  }

  return nullptr;
}
}  // Anonymous namespace

FilesystemPanel::FilesystemPanel(wxWindow* parent, wxWindowID id,
                                 const std::unique_ptr<DiscIO::Volume>& opened_iso)
    : wxPanel{parent, id}, m_opened_iso{opened_iso}
{
  CreateGUI();
  if (PopulateFileSystemTree())
  {
    BindEvents();
    m_tree_ctrl->Expand(m_tree_ctrl->GetRootItem());
  }
}

FilesystemPanel::~FilesystemPanel() = default;

void FilesystemPanel::BindEvents()
{
  m_tree_ctrl->Bind(wxEVT_TREE_ITEM_RIGHT_CLICK, &FilesystemPanel::OnRightClickTree, this);

  Bind(wxEVT_MENU, &FilesystemPanel::OnExtractFile, this, ID_EXTRACT_FILE);
  Bind(wxEVT_MENU, &FilesystemPanel::OnExtractDirectories, this, ID_EXTRACT_ALL);
  Bind(wxEVT_MENU, &FilesystemPanel::OnExtractDirectories, this, ID_EXTRACT_DIR);
  Bind(wxEVT_MENU, &FilesystemPanel::OnExtractHeaderData, this, ID_EXTRACT_APPLOADER);
  Bind(wxEVT_MENU, &FilesystemPanel::OnExtractHeaderData, this, ID_EXTRACT_DOL);
  Bind(wxEVT_MENU, &FilesystemPanel::OnCheckPartitionIntegrity, this, ID_CHECK_INTEGRITY);
}

void FilesystemPanel::CreateGUI()
{
  m_tree_ctrl = new wxTreeCtrl(this);
  m_tree_ctrl->AssignImageList(LoadIconBitmaps(this));
  m_tree_ctrl->AddRoot(_("Disc"), ICON_DISC);

  const auto space_5 = FromDIP(5);
  auto* const main_sizer = new wxBoxSizer(wxVERTICAL);
  main_sizer->AddSpacer(space_5);
  main_sizer->Add(m_tree_ctrl, 1, wxEXPAND | wxLEFT | wxRIGHT, space_5);
  main_sizer->AddSpacer(space_5);

  SetSizer(main_sizer);
}

bool FilesystemPanel::PopulateFileSystemTree()
{
  const std::vector<DiscIO::Partition> partitions = m_opened_iso->GetPartitions();
  m_has_partitions = !partitions.empty();

  if (m_has_partitions)
  {
    for (size_t i = 0; i < partitions.size(); ++i)
    {
      std::unique_ptr<DiscIO::FileSystem> file_system(
          DiscIO::CreateFileSystem(m_opened_iso.get(), partitions[i]));
      if (file_system)
      {
        wxTreeItemId partition_root = m_tree_ctrl->AppendItem(
            m_tree_ctrl->GetRootItem(), wxString::Format(_("Partition %zu"), i), ICON_DISC);

        WiiPartition* const partition = new WiiPartition(std::move(file_system));

        m_tree_ctrl->SetItemData(partition_root, partition);
        CreateDirectoryTree(m_tree_ctrl, partition_root, partition->filesystem->GetRoot());

        if (partitions[i] == m_opened_iso->GetGamePartition())
          m_tree_ctrl->Expand(partition_root);
      }
    }
  }
  else
  {
    m_filesystem = DiscIO::CreateFileSystem(m_opened_iso.get(), DiscIO::PARTITION_NONE);
    if (!m_filesystem)
      return false;

    CreateDirectoryTree(m_tree_ctrl, m_tree_ctrl->GetRootItem(), m_filesystem->GetRoot());
  }

  return true;
}

void FilesystemPanel::OnRightClickTree(wxTreeEvent& event)
{
  m_tree_ctrl->SelectItem(event.GetItem());

  wxMenu menu;

  const auto selection = m_tree_ctrl->GetSelection();
  const auto first_visible_item = m_tree_ctrl->GetFirstVisibleItem();
  const int image_type = m_tree_ctrl->GetItemImage(selection);

  if (image_type == ICON_DISC && first_visible_item != selection)
  {
    menu.Append(ID_EXTRACT_DIR, _("Extract Partition..."));
  }
  else if (image_type == ICON_FOLDER)
  {
    menu.Append(ID_EXTRACT_DIR, _("Extract Directory..."));
  }
  else if (image_type == ICON_FILE)
  {
    menu.Append(ID_EXTRACT_FILE, _("Extract File..."));
  }

  menu.Append(ID_EXTRACT_ALL, _("Extract All Files..."));

  if (!m_has_partitions || (image_type == ICON_DISC && first_visible_item != selection))
  {
    menu.AppendSeparator();
    menu.Append(ID_EXTRACT_APPLOADER, _("Extract Apploader..."));
    menu.Append(ID_EXTRACT_DOL, _("Extract DOL..."));
  }

  if (image_type == ICON_DISC && first_visible_item != selection)
  {
    menu.AppendSeparator();
    menu.Append(ID_CHECK_INTEGRITY, _("Check Partition Integrity"));
  }

  PopupMenu(&menu);
  event.Skip();
}

void FilesystemPanel::OnExtractFile(wxCommandEvent& WXUNUSED(event))
{
  const wxString selection_label = m_tree_ctrl->GetItemText(m_tree_ctrl->GetSelection());

  const wxString output_file_path =
      wxFileSelector(_("Extract File"), wxEmptyString, selection_label, wxEmptyString,
                     wxGetTranslation(wxALL_FILES), wxFD_SAVE, this);

  if (output_file_path.empty() || selection_label.empty())
    return;

  ExtractSingleFile(output_file_path);
}

void FilesystemPanel::OnExtractDirectories(wxCommandEvent& event)
{
  const wxString selected_directory_label = m_tree_ctrl->GetItemText(m_tree_ctrl->GetSelection());
  const wxString extract_path = wxDirSelector(_("Choose the folder to extract to"));

  if (extract_path.empty() || selected_directory_label.empty())
    return;

  switch (event.GetId())
  {
  case ID_EXTRACT_ALL:
    ExtractAllFiles(extract_path);
    break;
  case ID_EXTRACT_DIR:
    ExtractSingleDirectory(extract_path);
    break;
  }
}

void FilesystemPanel::OnExtractHeaderData(wxCommandEvent& event)
{
  DiscIO::FileSystem* file_system = nullptr;
  const wxString path = wxDirSelector(_("Choose the folder to extract to"));

  if (path.empty())
    return;

  if (m_has_partitions)
  {
    const auto* const selection_data = m_tree_ctrl->GetItemData(m_tree_ctrl->GetSelection());
    const auto* const partition = static_cast<const WiiPartition*>(selection_data);

    file_system = partition->filesystem.get();
  }
  else
  {
    file_system = m_filesystem.get();
  }

  bool ret = false;
  if (event.GetId() == ID_EXTRACT_APPLOADER)
  {
    ret = file_system->ExportApploader(WxStrToStr(path));
  }
  else if (event.GetId() == ID_EXTRACT_DOL)
  {
    ret = file_system->ExportDOL(WxStrToStr(path));
  }

  if (!ret)
  {
    WxUtils::ShowErrorDialog(
        wxString::Format(_("Failed to extract to %s!"), WxStrToStr(path).c_str()));
  }
}

void FilesystemPanel::OnCheckPartitionIntegrity(wxCommandEvent& WXUNUSED(event))
{
  // Normally we can't enter this function if we're analyzing a volume that
  // doesn't have partitions anyway, but let's still check to be sure.
  if (!m_has_partitions)
    return;

  wxProgressDialog dialog(_("Checking integrity..."), _("Working..."), 1000, this,
                          wxPD_APP_MODAL | wxPD_ELAPSED_TIME | wxPD_SMOOTH);

  const auto selection = m_tree_ctrl->GetSelection();
  WiiPartition* partition =
      static_cast<WiiPartition*>(m_tree_ctrl->GetItemData(m_tree_ctrl->GetSelection()));
  std::future<bool> is_valid = std::async(std::launch::async, [&] {
    return m_opened_iso->CheckIntegrity(partition->filesystem->GetPartition());
  });

  while (is_valid.wait_for(std::chrono::milliseconds(50)) != std::future_status::ready)
    dialog.Pulse();
  dialog.Hide();

  if (is_valid.get())
  {
    wxMessageBox(_("Integrity check completed. No errors have been found."),
                 _("Integrity check completed"), wxOK | wxICON_INFORMATION, this);
  }
  else
  {
    wxMessageBox(wxString::Format(_("Integrity check for %s failed. The disc image is most "
                                    "likely corrupted or has been patched incorrectly."),
                                  m_tree_ctrl->GetItemText(selection)),
                 _("Integrity Check Error"), wxOK | wxICON_ERROR, this);
  }
}

void FilesystemPanel::ExtractAllFiles(const wxString& output_folder)
{
  if (m_has_partitions)
  {
    const wxTreeItemId root = m_tree_ctrl->GetRootItem();

    wxTreeItemIdValue cookie;
    wxTreeItemId item = m_tree_ctrl->GetFirstChild(root, cookie);

    while (item.IsOk())
    {
      const auto* const partition = static_cast<WiiPartition*>(m_tree_ctrl->GetItemData(item));
      ExtractDirectories("", WxStrToStr(output_folder), *partition->filesystem);
      item = m_tree_ctrl->GetNextChild(root, cookie);
    }
  }
  else
  {
    ExtractDirectories("", WxStrToStr(output_folder), *m_filesystem);
  }
}

void FilesystemPanel::ExtractSingleFile(const wxString& output_file_path) const
{
  wxString selection_file_path = BuildFilePathFromSelection();

  if (m_has_partitions)
  {
    const size_t slash_index = selection_file_path.find('/');
    const wxString partition_label = selection_file_path.substr(0, slash_index);
    const auto* const partition = FindWiiPartition(m_tree_ctrl, partition_label);

    // Remove "Partition x/"
    selection_file_path.erase(0, slash_index + 1);

    partition->filesystem->ExportFile(
        partition->filesystem->FindFileInfo(WxStrToStr(selection_file_path)).get(),
        WxStrToStr(output_file_path));
  }
  else
  {
    m_filesystem->ExportFile(m_filesystem->FindFileInfo(WxStrToStr(selection_file_path)).get(),
                             WxStrToStr(output_file_path));
  }
}

void FilesystemPanel::ExtractSingleDirectory(const wxString& output_folder)
{
  wxString directory_path = BuildDirectoryPathFromSelection();

  if (m_has_partitions)
  {
    const size_t slash_index = directory_path.find('/');
    const wxString partition_label = directory_path.substr(0, slash_index);
    const auto* const partition = FindWiiPartition(m_tree_ctrl, partition_label);

    // Remove "Partition x/"
    directory_path.erase(0, slash_index + 1);

    ExtractDirectories(WxStrToStr(directory_path), WxStrToStr(output_folder),
                       *partition->filesystem);
  }
  else
  {
    ExtractDirectories(WxStrToStr(directory_path), WxStrToStr(output_folder), *m_filesystem);
  }
}

static void ExtractDir(const std::string& full_path, const std::string& output_folder,
                       const DiscIO::FileSystem& file_system, const DiscIO::FileInfo& directory,
                       const std::function<bool(const std::string& path)>& update_progress)
{
  for (const DiscIO::FileInfo& file_info : directory)
  {
    const std::string path = full_path + file_info.GetName() + (file_info.IsDirectory() ? "/" : "");
    const std::string output_path = output_folder + DIR_SEP_CHR + path;

    if (update_progress(path))
      return;

    DEBUG_LOG(DISCIO, "%s", output_path.c_str());

    if (file_info.IsDirectory())
    {
      File::CreateFullPath(output_path);
      ExtractDir(path, output_folder, file_system, file_info, update_progress);
    }
    else
    {
      if (File::Exists(output_path))
        NOTICE_LOG(DISCIO, "%s already exists", output_path.c_str());
      else if (!file_system.ExportFile(&file_info, output_path))
        ERROR_LOG(DISCIO, "Could not export %s", output_path.c_str());
    }
  }
}

void FilesystemPanel::ExtractDirectories(const std::string& full_path,
                                         const std::string& output_folder,
                                         const DiscIO::FileSystem& filesystem)
{
  if (full_path.empty())  // Root
  {
    filesystem.ExportApploader(output_folder);
    filesystem.ExportDOL(output_folder);
  }

  std::unique_ptr<DiscIO::FileInfo> file_info = filesystem.FindFileInfo(full_path);
  u32 size = file_info->GetTotalChildren();
  u32 progress = 0;

  wxString dialog_title = full_path.empty() ? _("Extracting All Files") : _("Extracting Directory");
  wxProgressDialog dialog(dialog_title, _("Extracting..."), size, this,
                          wxPD_APP_MODAL | wxPD_AUTO_HIDE | wxPD_CAN_ABORT | wxPD_ELAPSED_TIME |
                              wxPD_ESTIMATED_TIME | wxPD_REMAINING_TIME | wxPD_SMOOTH);

  File::CreateFullPath(output_folder + "/" + full_path);
  ExtractDir(full_path, output_folder, filesystem, *file_info, [&](const std::string& path) {
    dialog.SetTitle(wxString::Format(
        "%s : %d%%", dialog_title.c_str(),
        static_cast<u32>((static_cast<float>(progress) / static_cast<float>(size)) * 100)));
    dialog.Update(progress, wxString::Format(_("Extracting %s"), StrToWxStr(path)));
    ++progress;
    return dialog.WasCancelled();
  });
}

wxString FilesystemPanel::BuildFilePathFromSelection() const
{
  wxString file_path = m_tree_ctrl->GetItemText(m_tree_ctrl->GetSelection());

  const auto root_node = m_tree_ctrl->GetRootItem();
  auto node = m_tree_ctrl->GetItemParent(m_tree_ctrl->GetSelection());

  while (node != root_node)
  {
    file_path = m_tree_ctrl->GetItemText(node) + DIR_SEP_CHR + file_path;
    node = m_tree_ctrl->GetItemParent(node);
  }

  return file_path;
}

wxString FilesystemPanel::BuildDirectoryPathFromSelection() const
{
  wxString directory_path = BuildFilePathFromSelection();
  directory_path += DIR_SEP_CHR;
  return directory_path;
}
