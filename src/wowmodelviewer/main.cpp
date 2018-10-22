#include "logger/Logger.h"
#include "logger/LogOutputFile.h"
#include "logger/LogOutputConsole.h"

#include "WoWModelViewer.h"
#include "util.h"

#include <QtWidgets/QApplication>
#include <qsplashscreen.h>
#include <qtranslator.h>
#include <Qdir>


// tell wxwidgets which class is our app
//IMPLEMENT_APP_NO_MAIN(WowModelViewApp)

int main(int argc, char *argv[])
{
	//return wxEntry(argc, argv);		//Activates the wxApp version of WMV.

	// Build Application
	QApplication a(argc, argv);

	// List of Splash Screens to use
	QStringList splashes = { ":/Splashes/Splash_005_Alliance.png", ":/Splashes/Splash_005_Horde.png" };

	// Randomly Choose a splash screen
	srand(time(NULL));
	int randomchoice = rand() % splashes.count();
	QString splashImage = splashes.at(randomchoice);

	// Create & show the splash screen
	QPixmap pixmap(splashImage);
	QSplashScreen *splash = new QSplashScreen(pixmap, Qt::SplashScreen);
	splash->setAttribute(Qt::WA_TranslucentBackground);
	splash->show();
	double timerStart = getCurrentTime();

	// Keep going!
	a.processEvents();

	// Get our logfile working!
	if (QDir("./userSettings").exists() == false)
		QDir().mkpath("userSettings");
	LOGGER.addChild(new WMVLog::LogOutputFile("userSettings/log.txt"));
#if defined _DEBUG
	SetConsoleTitle(L"WoWModelViewer Debug Console");
	LOGGER.addChild(new WMVLog::LogOutputConsole());
#endif
	LOG_INFO << "Starting WoW Model Viewer...";

	WoWModelViewer w;

	// Everything defaults to English
	LOG_INFO << "Installing base localization...";
	QTranslator *translator = new QTranslator();
	if (translator->load(":/Translations/en_US.qm") == true)
	{
		a.installTranslator(translator);
		w.setTranslation("en_US");
	}
	else {
		qCritical("Unable to load default enUS locale for WoW Model Viewer.");
	}

	// Install default, System-Specific localization
	QString localeName = QLocale::system().name();
	qDebug() << "Looking for locale:" << localeName;
	if (localeName != "en_US")
	{
		LOG_INFO << "Installing system localization...";
		w.setTranslation(localeName);
	}


	//
	//for (int l = 0; l < locales.count(); l++)
	//{
	//	QString locale = locales.at(l);
	//	QTranslator *translator = new QTranslator();
	//	bool loaded = translator->load(":/Translations/" + locale + ".qm");
	//
	//	if (loaded == false || translator->isEmpty() == true)
	//	{
	//		qWarning("Unable to load locale \"%s\" for WoW Model Viewer.", qPrintable(locale));
	//		continue;
	//	}
	//	a.installTranslator(translator);
	//}
	//for (int i = 0; i < PluginList.count(); i++)
	//{
	//	wmvPlugin *piface = PluginList.at(i);
	//	logfile->Debug("Adding Locales for plugin \"%s\"...", qPrintable(piface->getPluginName()));
	//	for (int l = 0; l < locales.count(); l++)
	//	{
	//		QString locale = locales.at(l);
	//
	//		QTranslator *t = piface->pluginTranslator(locale);
	//		if (t == nullptr || t->isEmpty() == true)
	//		{
	//			logfile->Warn("Unable to load the Plugin \"%s\"'s locale \"%s\"...", qPrintable(piface->getPluginName()), qPrintable(locale));
	//			continue;
	//		}
	//		a.installTranslator(t);
	//	}
	//}

	// If it's been less than 5 seconds to load, wait for our splash to be appreciated
	double timerEnd = getCurrentTime();
	double timeSpent = timerEnd - timerStart;
	int sleepTime = 5000;		// Splash appreciation time, in milliseconds. (5000 = 5 seconds)
#ifdef _DEBUG
	// Give our debuggers a faster startup
	sleepTime = 1000;
#endif // _DEBUG
	sleepTime -= (int)timeSpent;
	if (sleepTime < 0) sleepTime = 0;
	Sleep(sleepTime);

	// Show our window when ready
	w.show();
	splash->finish(&w);			// Hide the splash as the main window shows up.

	return a.exec();
}