#pragma once

class GameFile;

#include "pugixml.hpp"

#include <string>
#include <vector>

/// @brief Manages custom tabard design details (icon, border, colours) and provides
///        texture lookup for rendering.
class TabardDetails
{
public:
	TabardDetails();

	bool showCustom;

	GameFile* GetIconTex(int slot);
	GameFile* GetBorderTex(int slot);
	GameFile* GetBackgroundTex(int slot);

	int GetMaxIcon();
	int GetMaxIconColor(int icon);
	int GetMaxBorder();
	int GetMaxBorderColor(int border);
	int GetMaxBackground();

	void save(pugi::xml_node& parentNode);
	void load(const pugi::xml_node& node);

	int getIcon();
	int getIconColor();
	int getBorder();
	int getBorderColor();
	int getBackground();

	void setIcon(int icon);
	void setIconColor(int color);
	void setBorder(int border);
	void setBorderColor(int color);
	void setBackground(int background);

	void setTabardId(int itemid);

	void setIconId(int id);
	void setBorderId(int id);
	void setBackgroundId(int id);

private:
	static const std::vector<std::string> ICON_COLOR_VECTOR;
	static const std::vector<std::string> BORDER_COLOR_VECTOR;
	static const std::vector<std::string> BACKGROUND_COLOR_VECTOR;

	int iconId;
	int iconColor;
	int borderId;
	int borderColor;
	int backgroundId;

	int tier;

	std::vector<int> backgrounds;
	std::vector<int> icons;
	std::vector<int> borders;
};
