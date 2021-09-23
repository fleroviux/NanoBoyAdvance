/*
 * Copyright (C) 2020 fleroviux
 *
 * Licensed under GPLv3 or any later version.
 * Refer to the included LICENSE file.
 */

#include <platform/device/sdl_audio_device.hpp>
#include <platform/loader/bios.hpp>
#include <platform/loader/rom.hpp>
#include <QApplication>
#include <QMenuBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QKeyEvent>
#include <unordered_map>

#include <iostream>

#include "main_window.hpp"

MainWindow::MainWindow(QApplication* app, QWidget* parent) : QMainWindow(parent) {
  setWindowTitle("NanoBoyAdvance");

  screen = std::make_shared<Screen>(this);
  setCentralWidget(screen.get());

  /* Read the configuration initially so that the correct configuration will be displayed. */
  ReadConfig();

  auto menubar = new QMenuBar(this);
  setMenuBar(menubar);

  CreateFileMenu(menubar);
  CreateOptionsMenu(menubar);
  CreateHelpMenu(menubar);

  /* Set emulator config */
  config->video_dev = screen;
  config->audio_dev = std::make_shared<nba::SDL2_AudioDevice>();
  config->input_dev = input_device;
  emulator = std::make_unique<nba::Emulator>(config);

  app->installEventFilter(this);

  keymap_window = new KeyMapWindow{app, this};

  FindGameController();
}

MainWindow::~MainWindow() {
  if (game_controller != nullptr) {
    SDL_GameControllerClose(game_controller);
  }
}

void MainWindow::CreateFileMenu(QMenuBar* menubar) {
  auto file_menu = menubar->addMenu(tr("&File"));
  auto file_open = file_menu->addAction(tr("&Open"));
  auto file_close = file_menu->addAction(tr("&Close"));

  connect(file_open, &QAction::triggered, this, &MainWindow::FileOpen);
  connect(file_close, &QAction::triggered, &QApplication::quit);
}

void MainWindow::CreateOptionsMenu(QMenuBar* menubar) {
  auto options_menu = menubar->addMenu(tr("&Options"));

  auto options_bios_menu = options_menu->addMenu(tr("BIOS"));
  auto options_bios_path = options_bios_menu->addAction(tr("Select path"));
  CreateBooleanOption(options_bios_menu, "Skip intro", &config->skip_bios);
  connect(options_bios_path, &QAction::triggered, [this] {
    QFileDialog dialog{this};
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setNameFilter("Game Boy Advance BIOS (*.bin)");

    if (!dialog.exec()) {
      return;
    }

    config->bios_path = dialog.selectedFiles().at(0).toStdString();
    WriteConfig();
  });

  auto options_cartridge_menu = options_menu->addMenu(tr("Cartridge"));
  CreateBooleanOption(options_cartridge_menu, "Force RTC", &config->force_rtc);
  CreateSelectionOption(options_cartridge_menu->addMenu(tr("Save Type")), {
    { "Detect",      nba::Config::BackupType::Detect },
    { "SRAM",        nba::Config::BackupType::SRAM   },
    { "FLASH 64K",   nba::Config::BackupType::FLASH_64  },
    { "FLASH 128K",  nba::Config::BackupType::FLASH_128 },
    { "EEPROM 512B", nba::Config::BackupType::EEPROM_4  },
    { "EEPROM 8K",   nba::Config::BackupType::EEPROM_64 }
  }, &config->backup_type);

  auto options_audio_menu = options_menu->addMenu(tr("Audio"));
  CreateSelectionOption(options_audio_menu->addMenu("Resampler"), {
    { "Cosine",   nba::Config::Audio::Interpolation::Cosine },
    { "Cubic",    nba::Config::Audio::Interpolation::Cubic  },
    { "Sinc-64",  nba::Config::Audio::Interpolation::Sinc_64  },
    { "Sinc-128", nba::Config::Audio::Interpolation::Sinc_128 },
    { "Sinc-256", nba::Config::Audio::Interpolation::Sinc_256 }
  }, &config->audio.interpolation);

  auto options_hq_audio_menu = options_audio_menu->addMenu("HQ audio mixer");
  CreateBooleanOption(options_hq_audio_menu, "Enable", &config->audio.mp2k_hle_enable);
  CreateBooleanOption(options_hq_audio_menu, "Use cubic filter", &config->audio.mp2k_hle_cubic);

  auto configure_input = options_menu->addAction(tr("Configure input"));
  connect(configure_input, &QAction::triggered, [this] {
    keymap_window->exec();
  });
}

void MainWindow::CreateBooleanOption(QMenu* menu, const char* name, bool* underlying) {
  auto action = menu->addAction(QString{name});
  auto config = this->config;

  action->setCheckable(true);
  action->setChecked(*underlying);

  connect(action, &QAction::triggered, [=](bool checked) {
    *underlying = checked;
    WriteConfig();
  });
}

void MainWindow::CreateHelpMenu(QMenuBar* menubar) {
  auto help_menu = menubar->addMenu(tr("&?"));
  auto about_app = help_menu->addAction(tr("About NanoBoyAdvance"));

  about_app->setMenuRole(QAction::AboutRole);
  connect(about_app, &QAction::triggered, [&] {
    QMessageBox box{ this };
    box.setTextFormat(Qt::RichText);
    box.setText(tr("NanoBoyAdvance is a Game Boy Advance emulator with a focus on high accuracy.<br><br>"
                   "Copyright © 2015 - 2021 fleroviux<br><br>"
                   "NanoBoyAdvance is licensed under the GPLv3 or any later version.<br><br>"
                   "GitHub: <a href=\"https://github.com/fleroviux/NanoBoyAdvance\">https://github.com/fleroviux/NanoBoyAdvance</a><br><br>"
                   "Game Boy Advance is a registered trademark of Nintendo Co., Ltd."));
    box.setWindowTitle(tr("About NanoBoyAdvance"));
    box.exec();
  });
}

void MainWindow::StartEmuThread() {
  emulator->Reset();
  emulator_state = EmulationState::Running;
  framelimiter.Reset();

  emulator_thread = std::thread([this] {
    emulator_thread_running = true;

    while (emulator_state == EmulationState::Running) {
      framelimiter.Run([&] {
        UpdateGameControllerInput();
        emulator->Frame();
      }, [&](int fps) {
        this->setWindowTitle(QString{ (std::string("NanoBoyAdvance [") + std::to_string(fps) + std::string(" fps]")).c_str() });
      });
    }

    emulator_thread_running = false;
  });

  emulator_thread.detach();  
}

bool MainWindow::LoadRom(const QString &file) {

  auto& core = emulator->GetCore();

  // TODO: clean this up!
  switch (nba::BIOSLoader::Load(core, config->bios_path)) {
    case nba::BIOSLoader::Result::CannotFindFile: {
      // TODO: ask the user if they want to assign the file.
      QMessageBox box {this};
      box.setText(tr("Please reference a BIOS file in the configuration.\n\n"
                     "Cannot find BIOS: ") + QString{config->bios_path.c_str()});
      box.setIcon(QMessageBox::Critical);
      box.setWindowTitle(tr("BIOS not found"));
      box.exec();
      return false;
    }
    case nba::BIOSLoader::Result::CannotOpenFile:
    case nba::BIOSLoader::Result::BadImage: {
      QMessageBox box {this};
      box.setText(tr("Failed to open the BIOS file. "
                     "Make sure that the permissions are correct and that the BIOS image is valid.\n\n"
                     "Cannot open BIOS: ") + QString{config->bios_path.c_str()});
      box.setIcon(QMessageBox::Critical);
      box.setWindowTitle(tr("Cannot open BIOS"));
      box.exec();
      return false;
    }
  }

  switch (nba::ROMLoader::Load(core, file.toStdString(), config->backup_type, config->force_rtc)) {
    case nba::ROMLoader::Result::CannotFindFile: {
      QMessageBox box {this};
      box.setText(tr("Cannot find file: ") + QFileInfo(file).fileName());
      box.setIcon(QMessageBox::Critical);
      box.setWindowTitle(tr("File not found"));
      box.exec();
      return false;
    }

    case nba::ROMLoader::Result::CannotOpenFile:
    case nba::ROMLoader::Result::BadImage: {
      QMessageBox box {this};
      box.setIcon(QMessageBox::Critical);
      box.setText(tr("The file could not be opened. Make sure that the permissions are correct and that the ROM image is valid."));
      box.setWindowTitle(tr("Cannot open ROM"));
      box.exec();
      return false;
    }
  }

  return true;
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
  auto type = event->type();

  if (type != QEvent::KeyPress && type != QEvent::KeyRelease) {
    return QObject::eventFilter(obj, event);
  }

  auto key = dynamic_cast<QKeyEvent*>(event)->key();
  auto pressed = type == QEvent::KeyPress;

  auto& map = keymap_window->map;

  for (int i = 0; i < nba::InputDevice::kKeyCount; i++) {
    if (map.keypad[i] == key) {
      input_device->SetKeyStatus(static_cast<nba::InputDevice::Key>(i), pressed);
    }
  }

  if (key == map.fast_forward) {
    framelimiter.SetFastForward(pressed);
    // return true;
  }

  return QObject::eventFilter(obj, event);
}

void MainWindow::FileOpen() {
  using StatusCode = nba::Emulator::StatusCode;

  QFileDialog dialog {this};
  dialog.setAcceptMode(QFileDialog::AcceptOpen);
  dialog.setFileMode(QFileDialog::ExistingFile);
  dialog.setNameFilter("Game Boy Advance ROMs (*.gba *.agb)");

  if (!dialog.exec()) {
    return;
  }

  auto file = dialog.selectedFiles().at(0);

  if (emulator_state == EmulationState::Running) {
    emulator_state = EmulationState::Stopped;
    while (emulator_thread_running) ;
  }

  /* Load emulator configuration */
  ReadConfig();

  auto& core = emulator->GetCore();

  if(loadRom(file))
  {
    startEmuThread();
  }
}

void MainWindow::FindGameController() {
  SDL_Init(SDL_INIT_GAMECONTROLLER);

  auto num_joysticks = SDL_NumJoysticks();

  for (int i = 0; i < num_joysticks; i++) {
    if (SDL_IsGameController(i)) {
      game_controller = SDL_GameControllerOpen(i);
      if (game_controller != nullptr) {
        nba::Log<nba::Info>("Qt: detected game controller '{0}'", SDL_GameControllerNameForIndex(i));
        break;
      }
    }
  }
}

void MainWindow::UpdateGameControllerInput() {
  using Key = nba::InputDevice::Key;

  if (game_controller == nullptr) {
    return;
  }

  SDL_GameControllerUpdate();

  auto button_x = SDL_GameControllerGetButton(game_controller, SDL_CONTROLLER_BUTTON_X);
  if (game_controller_button_x_old && !button_x) {
    framelimiter.SetFastForward(!framelimiter.GetFastForward());
  }
  game_controller_button_x_old = button_x;

  static const std::unordered_map<SDL_GameControllerButton, Key> buttons{
    { SDL_CONTROLLER_BUTTON_A, Key::A },
    { SDL_CONTROLLER_BUTTON_B, Key::B },
    { SDL_CONTROLLER_BUTTON_LEFTSHOULDER , Key::L },
    { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, Key::R },
    { SDL_CONTROLLER_BUTTON_START, Key::Start },
    { SDL_CONTROLLER_BUTTON_BACK, Key::Select }
  };

  // TODO: handle concurrent input from keyboard and game controller.

  for (auto& button : buttons) {
    if (SDL_GameControllerGetButton(game_controller, button.first)) {
      input_device->SetKeyStatus(button.second, true);
    } else {
      input_device->SetKeyStatus(button.second, false);
    }
  }

  constexpr auto threshold = std::numeric_limits<int16_t>::max() / 2;
  auto x = SDL_GameControllerGetAxis(game_controller, SDL_CONTROLLER_AXIS_LEFTX);
  auto y = SDL_GameControllerGetAxis(game_controller, SDL_CONTROLLER_AXIS_LEFTY);

  input_device->SetKeyStatus(Key::Left, x < -threshold);
  input_device->SetKeyStatus(Key::Right, x > threshold);
  input_device->SetKeyStatus(Key::Up, y < -threshold);
  input_device->SetKeyStatus(Key::Down, y > threshold);
}
