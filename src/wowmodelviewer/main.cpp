//#include <QtWidgets/QApplication>
//#include <qtranslator.h>
//#include "WoWModelViewer.h"
#include "app.h"

// tell wxwidgets which class is our app
IMPLEMENT_APP_NO_MAIN(WowModelViewApp)

int main(int argc, char *argv[])
{
	return wxEntry(argc, argv);		//Activates the wxApp version of WMV.

	/*
	// Qt version

	QApplication a(argc, argv);

	// Example of initial locate installation

	//	QStringList locales;
	//	locales << "en_US";
	//	
	//	qInfo("Installing localizations...");
	//	
	//	for (int l = 0; l < locales.count(); l++)
	//	{
	//		QString locale = locales.at(l);
	//		QTranslator *translator = new QTranslator();
	//		bool loaded = translator->load(":/Translations/" + locale + ".qm");
	//	
	//		if (loaded == false || translator->isEmpty() == true)
	//		{
	//			qWarning("Unable to load locale \"%s\" for WoW Model Viewer.", qPrintable(locale));
	//			continue;
	//		}
	//		a.installTranslator(translator);
	//	}
	//	for (int i = 0; i < PluginList.count(); i++)
	//	{
	//		wmvPlugin *piface = PluginList.at(i);
	//		logfile->Debug("Adding Locales for plugin \"%s\"...", qPrintable(piface->getPluginName()));
	//		for (int l = 0; l < locales.count(); l++)
	//		{
	//			QString locale = locales.at(l);
	//	
	//			QTranslator *t = piface->pluginTranslator(locale);
	//			if (t == nullptr || t->isEmpty() == true)
	//			{
	//				logfile->Warn("Unable to load the Plugin \"%s\"'s locale \"%s\"...", qPrintable(piface->getPluginName()), qPrintable(locale));
	//				continue;
	//			}
	//			a.installTranslator(t);
	//		}
	//	}

	WoWModelViewer w;
	w.show();

	return a.exec();
*/
}