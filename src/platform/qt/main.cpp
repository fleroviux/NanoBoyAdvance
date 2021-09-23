/*
 * Copyright (C) 2021 fleroviux
 *
 * Licensed under GPLv3 or any later version.
 * Refer to the included LICENSE file.
 */

#include <QApplication>

#include "main_window.hpp"

int main(int argc, char** argv) {
  QApplication app{ argc, argv };
  MainWindow window{ &app };

  QCoreApplication::setOrganizationName("fleroviux");
  QCoreApplication::setApplicationName("NanoBoyAdvance");

  window.show();

  /* If we are passed a rom as command line arg start the emu!*/
  const auto args = app.arguments();

  if(args.count() == 2 && window.loadRom(args[1]))
  {
    window.startEmuThread();
  }
  
  return app.exec();
}
