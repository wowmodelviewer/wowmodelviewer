/*
 * GlobalSettings.cpp
 *
 *  Created on: 26 nov. 2011
 *      Author: jeromnimo
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


// Namespaces used
//--------------------------------------------------------------------


// Beginning of implementation
//====================================================================

// Constructors 
//--------------------------------------------------------------------
GlobalSettings::GlobalSettings()
{
	m_versionMajorNumber = 0;
	m_versionMinorNumber = 7;
	m_versionMajorRevNumber = 0;
	m_versionMinorRevNumber = 5;

	m_appName = "WoW Model Viewer";
	m_buildName = "Chen Stormstout";

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
		

	*/

  // platform 
  m_platform = "Windows 32 bits";

#ifdef _BETAVERSION
  m_isBetaVersion = true;
#else
  m_isBetaVersion = false;
#endif

  // set it to whatever you want if you are building a special version
  m_versionSpecialExtend = "-mingw2";

}

// Destructor
//--------------------------------------------------------------------
GlobalSettings::~GlobalSettings()
{

}

// Public methods
//--------------------------------------------------------------------
GlobalSettings & GlobalSettings::instance()
{
	if(GlobalSettings::m_p_instance == 0)
	{
		GlobalSettings::m_p_instance = new GlobalSettings();
	}
	return *GlobalSettings::m_p_instance;
}

std::string GlobalSettings::appVersion(std::string a_prefix)
{
	std::string l_result = a_prefix;
	std::ostringstream l_oss;
	l_oss.precision(0);

	l_oss << m_versionMajorNumber << "." << m_versionMinorNumber << "."
			<< m_versionMajorRevNumber << "." << m_versionMinorRevNumber;
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
	return title;
}

// Protected methods
//--------------------------------------------------------------------


// Private methods
//--------------------------------------------------------------------
