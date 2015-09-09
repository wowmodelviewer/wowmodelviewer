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
 * GlobalSettings.cpp
 *
 *  Created on: 26 nov. 2011
 *   Copyright: 2013 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#define _GLOBALSETTINGS_CPP_
#include "GlobalSettings.h"
#undef _GLOBALSETTINGS_CPP_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL
#include <sstream>

#include <iostream>
// Wx

// Externals

// Other libraries

// Current library
#include "Plugin.h"

// Namespaces used
//--------------------------------------------------------------------


// Beginning of implementation
//====================================================================
GlobalSettings * GlobalSettings::m_instance = 0;

// Constructors 
//--------------------------------------------------------------------
GlobalSettings::GlobalSettings()
{
  m_versionMajorNumber = 0;
  m_versionMinorNumber = 8;
  m_versionRevNumber = 0;

  m_appName = "WoW Model Viewer";
  m_buildName = "Trip in Draenor";

  /*
		--==List of Build Name ideas==--	(Feel free to add!)
		Bouncing Baracuda
		Hoppin Jalapeno
		Stealthed Rogue
		Deadly Druid
		Killer Krakken
		Crazy Kaelthas
		Lonely Mastiff
		Cold Kelthuzad
		Jiggly Jaina
		Vashj's Folly
		Epic Win
		Epic Lose
		Lord Kezzak
		Perky Pug
		Great-father Winter


		--== Used Build Names ==--			(So we don't repeat...)
		Wascally Wabbit
		Gnome Punter
		Fickle Felguard
		Demented Deathwing
		Pickled Herring
		Windrunner's Lament
		Lost Lich King
    Skeer the Bloodseeker
    Thrall's revenge


   */

  // platform 
  m_platform = "Windows 32 bits";

#ifdef _BETAVERSION
  m_isBetaVersion = true;
#else
  m_isBetaVersion = false;
#endif

#ifdef _ALPHAVERSION
  m_isAlphaVersion = true;
#else
  m_isAlphaVersion = false;
#endif


  // set it to whatever you want if you are building a special version
  m_versionSpecialExtend = ".rc3";

  bShowParticle = false;
  bZeroParticle = false;
  bInitPoseOnlyExport = false;
}

// Destructor
//--------------------------------------------------------------------
GlobalSettings::~GlobalSettings()
{

}

// Public methods
//--------------------------------------------------------------------
std::string GlobalSettings::appVersion(std::string a_prefix)
{
  std::string l_result = a_prefix;
  std::ostringstream l_oss;
  l_oss.precision(0);

  l_oss << m_versionMajorNumber << "." << m_versionMinorNumber << "."
      << m_versionRevNumber;
  l_result += l_oss.str();

  l_result += m_versionSpecialExtend;

  return l_result;
}

std::string GlobalSettings::appName()
{
  return m_appName;
}

std::string GlobalSettings::buildName()
{
  return m_buildName;
}

std::string GlobalSettings::appTitle()
{
  std::string title = appName() + appVersion(std::string(" v")) + " " + m_platform;
  if(m_isBetaVersion)
    title += " - BETA VERSION";
  if(m_isAlphaVersion)
    title += " - ALPHA VERSION";
  return title;
}

// Protected methods
//--------------------------------------------------------------------


// Private methods
//--------------------------------------------------------------------
