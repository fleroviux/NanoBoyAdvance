/**
  * Copyright (C) 2017 flerovium^-^ (Frederic Meyer)
  *
  * This file is part of NanoboyAdvance.
  *
  * NanoboyAdvance is free software: you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation, either version 3 of the License, or
  * (at your option) any later version.
  * 
  * NanoboyAdvance is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  * GNU General Public License for more details.
  * 
  * You should have received a copy of the GNU General Public License
  * along with NanoboyAdvance. If not, see <http://www.gnu.org/licenses/>.
  */

#include "main_window.hpp"

MainWindow::MainWindow() {
  set_title("NanoboyAdvance 0.1");
  set_reallocate_redraws(true); //TODO: wat is dis
  
  add(m_vbox);
  
  // setup screen
  m_screen.set_size_request(240, 160);
  m_vbox.pack_start(m_screen);
  
  show_all();
}

MainWindow::~MainWindow() {
  
}