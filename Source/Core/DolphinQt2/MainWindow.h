// Copyright 2015 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <QMainWindow>
#include <QStackedWidget>
#include <QString>
#include <QToolBar>

#include "DolphinQt2/GameList/GameList.h"
#include "DolphinQt2/MenuBar.h"
#include "DolphinQt2/RenderWidget.h"
#include "DolphinQt2/ToolBar.h"

class HotkeyScheduler;
class MappingWindow;
class SettingsWindow;
class ControllersWindow;

class MainWindow final : public QMainWindow
{
  Q_OBJECT

public:
  explicit MainWindow();
  ~MainWindow();

  bool eventFilter(QObject* object, QEvent* event) override;

signals:
  void EmulationStarted();
  void EmulationPaused();
  void EmulationStopped();

private slots:
  void Open();
  void Play();
  void Pause();

  // May ask for confirmation. Returns whether or not it actually stopped.
  bool Stop();
  void ForceStop();
  void Reset();
  void FrameAdvance();
  void StateLoad();
  void StateSave();
  void StateLoadSlot();
  void StateSaveSlot();
  void StateLoadSlotAt(int slot);
  void StateSaveSlotAt(int slot);
  void StateLoadUndo();
  void StateSaveUndo();
  void StateSaveOldest();
  void SetStateSlot(int slot);

  void FullScreen();
  void ScreenShot();

private:
  void CreateComponents();

  void ConnectGameList();
  void ConnectHotkeys();
  void ConnectMenuBar();
  void ConnectRenderWidget();
  void ConnectStack();
  void ConnectToolBar();

  void InitControllers();
  void ShutdownControllers();

  void InitCoreCallbacks();

  void StartGame(const QString& path);
  void ShowRenderWidget();
  void HideRenderWidget();

  void ShowSettingsWindow();
  void ShowControllersWindow();
  void ShowAboutDialog();
  void ShowHotkeyDialog();

  QStackedWidget* m_stack;
  ToolBar* m_tool_bar;
  MenuBar* m_menu_bar;
  GameList* m_game_list;
  RenderWidget* m_render_widget;
  bool m_rendering_to_main;
  bool m_stop_requested = false;
  int m_state_slot = 1;

  HotkeyScheduler* m_hotkey_scheduler;
  ControllersWindow* m_controllers_window;
  SettingsWindow* m_settings_window;
  MappingWindow* m_hotkey_window;
};
