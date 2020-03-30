/*
 * TabardDetails.h
 *
 *  Created on: 26 oct. 2013
 *
 */

#ifndef _TABARDDETAILS_H_
#define _TABARDDETAILS_H_

#include <QString>

class QXmlStreamReader;
class QXmlStreamWriter;

#include <map>
#include <vector>

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _TABARDDETAILS_API_ __declspec(dllexport)
#    else
#        define _TABARDDETAILS_API_ __declspec(dllimport)
#    endif
#else
#    define _TABARDDETAILS_API_
#endif

class _TABARDDETAILS_API_ TabardDetails
{
  public:

    TabardDetails();

    int Icon;
    int IconColor;
    int Border;
    int BorderColor;
    int Background;

    bool showCustom;

    QString GetIconTex(int slot);
    QString GetBorderTex(int slot);
    QString GetBackgroundTex(int slot);

    int GetMaxIcon();
    int GetMaxIconColor(int icon);
    int GetMaxBorder();
    int GetMaxBorderColor(int border);
    int GetMaxBackground();

    void save(QXmlStreamWriter &);
    void load(QXmlStreamReader &);

  private:
	  static const std::vector<QString> ICON_COLOR_VECTOR;
	  static const std::vector<QString> BORDER_COLOR_VECTOR;
	  static const std::vector<QString> BACKGROUND_COLOR_VECTOR;

    int maxIcon;
    std::map<int, int> maxIconColorMap;
    int maxBorder;
    std::map<int, int> maxBorderColorMap;
    int maxBackground;
};


#endif /* _TABARDDETAILS_H_ */
