// Copyright 2015 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QIcon>
#include <QMessageBox>

#include "Common/Common.h"

#include "Core/Boot/Boot.h"
#include "Core/BootManager.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/HW/GCKeyboard.h"
#include "Core/HW/GCPad.h"
#include "Core/HW/ProcessorInterface.h"
#include "Core/HW/Wiimote.h"
#include "Core/HW/WiimoteEmu/WiimoteEmu.h"
#include "Core/HotkeyManager.h"
#include "Core/Movie.h"
#include "Core/NetPlayProto.h"
#include "Core/State.h"

#include "DolphinQt2/AboutDialog.h"
#include "DolphinQt2/Config/ControllersWindow.h"

#include "DolphinQt2/Config/Mapping/MappingWindow.h"
#include "DolphinQt2/Config/SettingsWindow.h"
#include "DolphinQt2/Host.h"
#include "DolphinQt2/HotkeyScheduler.h"
#include "DolphinQt2/MainWindow.h"
#include "DolphinQt2/QtUtils/WindowActivationEventFilter.h"
#include "DolphinQt2/Resources.h"
#include "DolphinQt2/Settings.h"

#include "InputCommon/ControllerInterface/ControllerInterface.h"

#include "UICommon/UICommon.h"

MainWindow::MainWindow() : QMainWindow(nullptr)
{
  setWindowTitle(QString::fromStdString(scm_rev_str));
  setWindowIcon(QIcon(Resources::GetMisc(Resources::LOGO_SMALL)));
  setUnifiedTitleAndToolBarOnMac(true);

  CreateComponents();

  ConnectGameList();
  ConnectToolBar();
  ConnectRenderWidget();
  ConnectStack();
  ConnectMenuBar();

  InitControllers();
  InitCoreCallbacks();
}

MainWindow::~MainWindow()
{
  m_render_widget->deleteLater();
  ShutdownControllers();
}

void MainWindow::InitControllers()
{
  if (g_controller_interface.IsInit())
    return;

  g_controller_interface.Initialize(reinterpret_cast<void*>(winId()));
  Pad::Initialize();
  Keyboard::Initialize();
  Wiimote::Initialize(Wiimote::InitializeMode::DO_NOT_WAIT_FOR_WIIMOTES);
  m_hotkey_scheduler = new HotkeyScheduler();
  m_hotkey_scheduler->Start();

  ConnectHotkeys();
}

void MainWindow::ShutdownControllers()
{
  m_hotkey_scheduler->Stop();

  g_controller_interface.Shutdown();
  Pad::Shutdown();
  Keyboard::Shutdown();
  Wiimote::Shutdown();
  HotkeyManagerEmu::Shutdown();

  m_hotkey_scheduler->deleteLater();
}

void MainWindow::InitCoreCallbacks()
{
  Core::SetOnStoppedCallback([this] { emit EmulationStopped(); });
  installEventFilter(this);
  m_render_widget->installEventFilter(this);
}

static void InstallHotkeyFilter(QWidget* dialog)
{
  auto* filter = new WindowActivationEventFilter();
  dialog->installEventFilter(filter);

  filter->connect(filter, &WindowActivationEventFilter::windowDeactivated,
                  [] { HotkeyManagerEmu::Enable(true); });
  filter->connect(filter, &WindowActivationEventFilter::windowActivated,
                  [] { HotkeyManagerEmu::Enable(false); });
}

void MainWindow::CreateComponents()
{
  m_menu_bar = new MenuBar(this);
  m_tool_bar = new ToolBar(this);
  m_game_list = new GameList(this);
  m_render_widget = new RenderWidget;
  m_stack = new QStackedWidget(this);
  m_controllers_window = new ControllersWindow(this);
  m_settings_window = new SettingsWindow(this);
  m_hotkey_window = new MappingWindow(this, 0);

  InstallHotkeyFilter(m_hotkey_window);
  InstallHotkeyFilter(m_controllers_window);
  InstallHotkeyFilter(m_settings_window);
}

void MainWindow::ConnectMenuBar()
{
  setMenuBar(m_menu_bar);
  // File
  connect(m_menu_bar, &MenuBar::Open, this, &MainWindow::Open);
  connect(m_menu_bar, &MenuBar::Exit, this, &MainWindow::close);

  // Emulation
  connect(m_menu_bar, &MenuBar::Pause, this, &MainWindow::Pause);
  connect(m_menu_bar, &MenuBar::Play, this, &MainWindow::Play);
  connect(m_menu_bar, &MenuBar::Stop, this, &MainWindow::Stop);
  connect(m_menu_bar, &MenuBar::Reset, this, &MainWindow::Reset);
  connect(m_menu_bar, &MenuBar::Fullscreen, this, &MainWindow::FullScreen);
  connect(m_menu_bar, &MenuBar::FrameAdvance, this, &MainWindow::FrameAdvance);
  connect(m_menu_bar, &MenuBar::Screenshot, this, &MainWindow::ScreenShot);
  connect(m_menu_bar, &MenuBar::StateLoad, this, &MainWindow::StateLoad);
  connect(m_menu_bar, &MenuBar::StateSave, this, &MainWindow::StateSave);
  connect(m_menu_bar, &MenuBar::StateLoadSlot, this, &MainWindow::StateLoadSlot);
  connect(m_menu_bar, &MenuBar::StateSaveSlot, this, &MainWindow::StateSaveSlot);
  connect(m_menu_bar, &MenuBar::StateLoadSlotAt, this, &MainWindow::StateLoadSlotAt);
  connect(m_menu_bar, &MenuBar::StateSaveSlotAt, this, &MainWindow::StateSaveSlotAt);
  connect(m_menu_bar, &MenuBar::StateLoadUndo, this, &MainWindow::StateLoadUndo);
  connect(m_menu_bar, &MenuBar::StateSaveUndo, this, &MainWindow::StateSaveUndo);
  connect(m_menu_bar, &MenuBar::StateSaveOldest, this, &MainWindow::StateSaveOldest);
  connect(m_menu_bar, &MenuBar::SetStateSlot, this, &MainWindow::SetStateSlot);

  // Options
  connect(m_menu_bar, &MenuBar::ConfigureHotkeys, this, &MainWindow::ShowHotkeyDialog);

  // View
  connect(m_menu_bar, &MenuBar::ShowTable, m_game_list, &GameList::SetTableView);
  connect(m_menu_bar, &MenuBar::ShowList, m_game_list, &GameList::SetListView);
  connect(m_menu_bar, &MenuBar::ColumnVisibilityToggled, m_game_list,
          &GameList::OnColumnVisibilityToggled);
  connect(m_menu_bar, &MenuBar::ShowAboutDialog, this, &MainWindow::ShowAboutDialog);

  connect(this, &MainWindow::EmulationStarted, m_menu_bar, &MenuBar::EmulationStarted);
  connect(this, &MainWindow::EmulationPaused, m_menu_bar, &MenuBar::EmulationPaused);
  connect(this, &MainWindow::EmulationStopped, m_menu_bar, &MenuBar::EmulationStopped);

  connect(this, &MainWindow::EmulationStarted, this,
          [=]() { m_controllers_window->OnEmulationStateChanged(true); });
  connect(this, &MainWindow::EmulationStopped, this,
          [=]() { m_controllers_window->OnEmulationStateChanged(false); });
}

void MainWindow::ConnectHotkeys()
{
  connect(m_hotkey_scheduler, &HotkeyScheduler::ExitHotkey, this, &MainWindow::close);
  connect(m_hotkey_scheduler, &HotkeyScheduler::PauseHotkey, this, &MainWindow::Pause);
  connect(m_hotkey_scheduler, &HotkeyScheduler::StopHotkey, this, &MainWindow::Stop);
  connect(m_hotkey_scheduler, &HotkeyScheduler::ScreenShotHotkey, this, &MainWindow::ScreenShot);
  connect(m_hotkey_scheduler, &HotkeyScheduler::FullScreenHotkey, this, &MainWindow::FullScreen);

  connect(m_hotkey_scheduler, &HotkeyScheduler::StateLoadSlotHotkey, this,
          &MainWindow::StateLoadSlot);
  connect(m_hotkey_scheduler, &HotkeyScheduler::StateSaveSlotHotkey, this,
          &MainWindow::StateSaveSlot);
  connect(m_hotkey_scheduler, &HotkeyScheduler::SetStateSlotHotkey, this,
          &MainWindow::SetStateSlot);
}

void MainWindow::ConnectToolBar()
{
  addToolBar(m_tool_bar);
  connect(m_tool_bar, &ToolBar::OpenPressed, this, &MainWindow::Open);
  connect(m_tool_bar, &ToolBar::PlayPressed, this, &MainWindow::Play);
  connect(m_tool_bar, &ToolBar::PausePressed, this, &MainWindow::Pause);
  connect(m_tool_bar, &ToolBar::StopPressed, this, &MainWindow::Stop);
  connect(m_tool_bar, &ToolBar::FullScreenPressed, this, &MainWindow::FullScreen);
  connect(m_tool_bar, &ToolBar::ScreenShotPressed, this, &MainWindow::ScreenShot);
  connect(m_tool_bar, &ToolBar::SettingsPressed, this, &MainWindow::ShowSettingsWindow);
  connect(m_tool_bar, &ToolBar::ControllersPressed, this, &MainWindow::ShowControllersWindow);

  connect(this, &MainWindow::EmulationStarted, m_tool_bar, &ToolBar::EmulationStarted);
  connect(this, &MainWindow::EmulationPaused, m_tool_bar, &ToolBar::EmulationPaused);
  connect(this, &MainWindow::EmulationStopped, m_tool_bar, &ToolBar::EmulationStopped);

  connect(this, &MainWindow::EmulationStopped, [this] {
    m_stop_requested = false;
    m_render_widget->hide();
  });
}

void MainWindow::ConnectGameList()
{
  connect(m_game_list, &GameList::GameSelected, this, &MainWindow::Play);
}

void MainWindow::ConnectRenderWidget()
{
  m_rendering_to_main = false;
  m_render_widget->hide();
  connect(m_render_widget, &RenderWidget::EscapePressed, this, &MainWindow::Stop);
  connect(m_render_widget, &RenderWidget::Closed, this, &MainWindow::ForceStop);
}

void MainWindow::ConnectStack()
{
  m_stack->setMinimumSize(800, 600);
  m_stack->addWidget(m_game_list);
  setCentralWidget(m_stack);
}

void MainWindow::Open()
{
  QString file = QFileDialog::getOpenFileName(
      this, tr("Select a File"), QDir::currentPath(),
      tr("All GC/Wii files (*.elf *.dol *.gcm *.iso *.tgc *.wbfs *.ciso *.gcz *.wad);;"
         "All Files (*)"));
  if (!file.isEmpty())
    StartGame(file);
}

void MainWindow::Play()
{
  // If we're in a paused game, start it up again.
  // Otherwise, play the selected game, if there is one.
  // Otherwise, play the default game.
  // Otherwise, play the last played game, if there is one.
  // Otherwise, prompt for a new game.
  if (Core::GetState() == Core::State::Paused)
  {
    Core::SetState(Core::State::Running);
    emit EmulationStarted();
  }
  else
  {
    QString selection = m_game_list->GetSelectedGame();
    if (selection.length() > 0)
    {
      StartGame(selection);
    }
    else
    {
      QString default_path = Settings::Instance().GetDefaultGame();
      if (!default_path.isEmpty() && QFile::exists(default_path))
      {
        StartGame(default_path);
      }
      else
      {
        Open();
      }
    }
  }
}

void MainWindow::Pause()
{
  Core::SetState(Core::State::Paused);
  emit EmulationPaused();
}

bool MainWindow::Stop()
{
  if (!Core::IsRunning())
    return true;

  if (Settings::Instance().GetConfirmStop())
  {
    const Core::State state = Core::GetState();
    // Set to false when Netplay is running as a CPU thread
    bool pause = true;

    if (pause)
      Core::SetState(Core::State::Paused);

    QMessageBox::StandardButton confirm;
    confirm = QMessageBox::question(m_render_widget, tr("Confirm"),
                                    m_stop_requested ?
                                        tr("A shutdown is already in progress. Unsaved data "
                                           "may be lost if you stop the current emulation "
                                           "before it completes. Force stop?") :
                                        tr("Do you want to stop the current emulation?"));
    if (pause)
      Core::SetState(state);

    if (confirm != QMessageBox::Yes)
      return false;
  }

  // TODO: Add Movie shutdown
  // TODO: Add Debugger shutdown

  if (!m_stop_requested && UICommon::TriggerSTMPowerEvent())
  {
    m_stop_requested = true;

    // Unpause because gracefully shutting down needs the game to actually request a shutdown.
    // Do not unpause in debug mode to allow debugging until the complete shutdown.
    if (Core::GetState() == Core::State::Paused)
      Core::SetState(Core::State::Running);

    return false;
  }

  ForceStop();

#ifdef Q_OS_WIN
  // Allow windows to idle or turn off display again
  SetThreadExecutionState(ES_CONTINUOUS);
#endif
  return true;
}

void MainWindow::ForceStop()
{
  BootManager::Stop();
  HideRenderWidget();
}

void MainWindow::Reset()
{
  if (Movie::IsRecordingInput())
    Movie::SetReset(true);
  ProcessorInterface::ResetButton_Tap();
}

void MainWindow::FrameAdvance()
{
  Movie::DoFrameStep();
  EmulationPaused();
}

void MainWindow::FullScreen()
{
  // If the render widget is fullscreen we want to reset it to whatever is in
  // settings. If it's set to be fullscreen then it just remakes the window,
  // which probably isn't ideal.
  bool was_fullscreen = m_render_widget->isFullScreen();
  HideRenderWidget();
  if (was_fullscreen)
    ShowRenderWidget();
  else
    m_render_widget->showFullScreen();
}

void MainWindow::ScreenShot()
{
  Core::SaveScreenShot();
}

void MainWindow::StartGame(const QString& path)
{
  // If we're running, only start a new game once we've stopped the last.
  if (Core::GetState() != Core::State::Uninitialized)
  {
    if (!Stop())
      return;
  }
  // Boot up, show an error if it fails to load the game.
  if (!BootManager::BootCore(BootParameters::GenerateFromFile(path.toStdString())))
  {
    QMessageBox::critical(this, tr("Error"), tr("Failed to init core"), QMessageBox::Ok);
    return;
  }
  ShowRenderWidget();
  emit EmulationStarted();

#ifdef Q_OS_WIN
  // Prevents Windows from sleeping, turning off the display, or idling
  EXECUTION_STATE shouldScreenSave =
      SConfig::GetInstance().bDisableScreenSaver ? ES_DISPLAY_REQUIRED : 0;
  SetThreadExecutionState(ES_CONTINUOUS | shouldScreenSave | ES_SYSTEM_REQUIRED);
#endif
}

void MainWindow::ShowRenderWidget()
{
  auto& settings = Settings::Instance();
  if (settings.GetRenderToMain())
  {
    // If we're rendering to main, add it to the stack and update our title when necessary.
    m_rendering_to_main = true;
    m_stack->setCurrentIndex(m_stack->addWidget(m_render_widget));
    connect(Host::GetInstance(), &Host::RequestTitle, this, &MainWindow::setWindowTitle);
  }
  else
  {
    // Otherwise, just show it.
    m_rendering_to_main = false;
    if (settings.GetFullScreen())
    {
      m_render_widget->showFullScreen();
    }
    else
    {
      m_render_widget->resize(settings.GetRenderWindowSize());
      m_render_widget->showNormal();
    }
  }
}

void MainWindow::HideRenderWidget()
{
  if (m_rendering_to_main)
  {
    // Remove the widget from the stack and reparent it to nullptr, so that it can draw
    // itself in a new window if it wants. Disconnect the title updates.
    m_stack->removeWidget(m_render_widget);
    m_render_widget->setParent(nullptr);
    m_rendering_to_main = false;
    disconnect(Host::GetInstance(), &Host::RequestTitle, this, &MainWindow::setWindowTitle);
    setWindowTitle(QString::fromStdString(scm_rev_str));
  }
  m_render_widget->hide();
}

void MainWindow::ShowControllersWindow()
{
  m_controllers_window->show();
  m_controllers_window->raise();
  m_controllers_window->activateWindow();
}

void MainWindow::ShowSettingsWindow()
{
  m_settings_window->show();
  m_settings_window->raise();
  m_settings_window->activateWindow();
}

void MainWindow::ShowAboutDialog()
{
  AboutDialog* about = new AboutDialog(this);
  about->show();
}

void MainWindow::ShowHotkeyDialog()
{
  m_hotkey_window->ChangeMappingType(MappingWindow::Type::MAPPING_HOTKEYS);
  m_hotkey_window->show();
  m_hotkey_window->raise();
  m_hotkey_window->activateWindow();
}

void MainWindow::StateLoad()
{
  QString path = QFileDialog::getOpenFileName(this, tr("Select a File"), QDir::currentPath(),
                                              tr("All Save States (*.sav *.s##);; All Files (*)"));
  State::LoadAs(path.toStdString());
}

void MainWindow::StateSave()
{
  QString path = QFileDialog::getSaveFileName(this, tr("Select a File"), QDir::currentPath(),
                                              tr("All Save States (*.sav *.s##);; All Files (*)"));
  State::SaveAs(path.toStdString());
}

void MainWindow::StateLoadSlot()
{
  State::Load(m_state_slot);
}

void MainWindow::StateSaveSlot()
{
  State::Save(m_state_slot, true);
  m_menu_bar->UpdateStateSlotMenu();
}

void MainWindow::StateLoadSlotAt(int slot)
{
  State::Load(slot);
}

void MainWindow::StateSaveSlotAt(int slot)
{
  State::Save(slot, true);
  m_menu_bar->UpdateStateSlotMenu();
}

void MainWindow::StateLoadUndo()
{
  State::UndoLoadState();
}

void MainWindow::StateSaveUndo()
{
  State::UndoSaveState();
}

void MainWindow::StateSaveOldest()
{
  State::SaveFirstSaved();
}

void MainWindow::SetStateSlot(int slot)
{
  Settings::Instance().SetStateSlot(slot);
  m_state_slot = slot;
}

bool MainWindow::eventFilter(QObject* object, QEvent* event)
{
  if (event->type() == QEvent::Close && !Stop())
  {
    static_cast<QCloseEvent*>(event)->ignore();
    return true;
  }

  return false;
}
