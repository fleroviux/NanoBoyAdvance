/*
 * Copyright (C) 2020 fleroviux
 *
 * Licensed under GPLv3 or any later version.
 * Refer to the included LICENSE file.
 */

#pragma once

#include <atomic>
#include <platform/config.hpp>
#include <platform/emulator.hpp>
#include <platform/frame_limiter.hpp>
#include <memory>
#include <QMainWindow>
#include <QActionGroup>
#include <QMenu>
#include <SDL.h>
#include <utility>
#include <vector>

#include "keymap_window.hpp"
#include "screen.hpp"

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  MainWindow(QApplication* app, QWidget* parent = 0);
 ~MainWindow();

  bool LoadRom(const QString &file);
  void StartEmuThread();

protected:
  bool eventFilter(QObject* obj, QEvent* event);

private slots:
  void FileOpen();

private:
  void CreateFileMenu(QMenuBar* menubar);
  void CreateOptionsMenu(QMenuBar* menubar);
  void CreateHelpMenu(QMenuBar* menubar);
  void CreateBooleanOption(QMenu* menu, const char* name, bool* underlying);

  template <typename T>
  void CreateSelectionOption(QMenu* menu, std::vector<std::pair<std::string, T>> const& mapping, T* underlying) {
    auto group = new QActionGroup{this};
    auto config = this->config;

    for (auto& entry : mapping) {
      auto action = group->addAction(QString::fromStdString(entry.first));
      action->setCheckable(true);
      action->setChecked(*underlying == entry.second);

      connect(action, &QAction::triggered, [=]() {
        *underlying = entry.second;
        nba::config_toml_write(*config, "config.toml");
      });
    }

    menu->addActions(group->actions());
  }

  static constexpr auto s_toml_config_path = "config.toml";

  void ReadConfig() {
    nba::config_toml_read(*config, s_toml_config_path);
  }

  void WriteConfig() {
    nba::config_toml_write(*config, s_toml_config_path);
  }

  void FindGameController();
  void UpdateGameControllerInput();

  enum class EmulationState {
    Stopped,
    Running,
    Paused
  } emulator_state = EmulationState::Stopped;

  std::atomic<bool> emulator_thread_running;

  std::shared_ptr<Screen> screen;
  std::shared_ptr<nba::BasicInputDevice> input_device = std::make_shared<nba::BasicInputDevice>();
  std::shared_ptr<nba::Config> config = std::make_shared<nba::Config>();
  std::unique_ptr<nba::Emulator> emulator;
  nba::FrameLimiter framelimiter {59.7275};
  std::thread emulator_thread;

  KeyMapWindow* keymap_window;

  SDL_GameController* game_controller = nullptr;
  bool game_controller_button_x_old = false;
};
