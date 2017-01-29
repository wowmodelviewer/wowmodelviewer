/*----------------------------------------------------------------------*\
| This file is part of WoW Model Viewer                                  |
|                                                                        |
| WoW Model Viewer is free software: you can redistribute it and/or      |
| modify it under the terms of the GNU General Public License as         |
| published by the Free Software Foundation, either version 3 of the     |
| License, or (at your option) any later version.                        |
|                                                                        |
| WoW Model Viewer is distributed in the hope that it will be useful,    |
| but WITHOUT ANY WARRANTY; without even the implied warranty of         |
| MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          |
| GNU General Public License for more details.                           |
|                                                                        |
| You should have received a copy of the GNU General Public License      |
| along with WoW Model Viewer.                                           |
| If not, see <http://www.gnu.org/licenses/>.                            |
\*----------------------------------------------------------------------*/


/*
 * main.cpp
 *
 *  Created on: 29 jan. 2017
 *   Copyright: 2017 , WoW Model Viewer (http://wowmodelviewer.net)
 */


#pragma comment(linker, "/SUBSYSTEM:CONSOLE")

#include <iostream>

#include <QFile>
#include <QString>
#include <QTextStream>

#include "CascLib.h"

int main(int argc, char ** argv)
{
  if (argc < 4)
  {
    std::cout << "Usage " << argv[0] << "[input listfile] [data folder] [output listfile]" << std::endl;
    return 1;
  }

  QString inputFile(argv[1]);
  QString dataFoler(argv[2]);
  QString outputFile(argv[3]);

  std::cout << "Input File : " << inputFile.toStdString() << std::endl;
  std::cout << "Data Folder : " << dataFoler.toStdString() << std::endl;
  std::cout << "Output File : " << outputFile.toStdString() << std::endl;

  HANDLE CascStorage;
  
  if (!CascOpenStorage(dataFoler.toStdString().c_str(), CASC_LOCALE_ENUS, &CascStorage))
  {
    std::cout << "Impossible to open CASC storage : " << dataFoler.toStdString() << std::endl;
    return 2;
  }

  QFile infile(inputFile);
  if (!infile.open(QIODevice::ReadOnly | QIODevice::Text))
  {
    std::cout << "Fail to open " << inputFile.toStdString() << std::endl;
    return 3;
  }

  QFile outfile(outputFile);
  if (!outfile.open(QIODevice::ReadWrite | QIODevice::Text))
  {
    std::cout << "Fail to open " << outputFile.toStdString() << std::endl;
    return 4;
  }

  QTextStream in(&infile);
  QTextStream out(&outfile);

  while (!in.atEnd())
  {
    QString line = in.readLine().toLower();

    // just in case listfile already contains file data ids
    QStringList list = line.split(" ");
    QString filename = list[0];

    out << filename << " " << CascGetFileId(CascStorage, filename.toStdString().c_str()) << endl;
  }

  return 0;
}
