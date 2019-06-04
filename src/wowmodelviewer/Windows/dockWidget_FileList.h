#pragma once
#include "ui_dockWidget_FileList.h"

struct treeItem
{
	QString name;
	QList<treeItem*> children;
};

class FileTreeModel : public QAbstractItemModel
{
	Q_OBJECT

public:
	void a();
	FileTreeModel(QObject *parent = nullptr);
	~FileTreeModel();

	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	QVariant data(const QModelIndex &index, int role) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

	int treeContains(QList<treeItem*> itemList, QString name);
	void addToTree(QString name);

private:
	QList<treeItem*> items;
};

class dockWidgetFileList : public QWidget
{
	Q_OBJECT

	enum FilterModes {
		FILE_FILTER_MODEL = 0,
		FILE_FILTER_WMO,
		FILE_FILTER_ADT,
		FILE_FILTER_WAV,
		FILE_FILTER_MP3,
		FILE_FILTER_IMAGE,
		FILE_FILTER_BLS,
		FILE_FILTER_DBC,
		FILE_FILTER_DB2,
		FILE_FILTER_LUA,
		FILE_FILTER_XML,

		FILE_FILTER_MAX
	};

	/*
	All suffixs in MPQ:
	.adt .anim .blob .BLP .bls .bundle .cfg .css .db .dbc .DELETE .dll .error .exe
	.gif .html .icns .ini .jpg .js .log .lua .M2 .mp3 .mpq .nib .not .pdf .plist .png
	.rsrc .sbt .SIG .skin .test .tiff .toc .trs .TTF .txt .url .uvw .wav .wdl .wdt
	.wfx .what .wmo .wtf .xib .xml .xsd .zmp
	*/
	QStringList filterStrings = { "m2", "wmo", "adt", "wav", "ogg", "mp3", "blp", "bls", "dbc", "db2", "lua", "xml", "skin" };
	QStringList chos = { tr("Models (*.m2)"), tr("WMOs (*.wmo)"), tr("ADTs (*.adt)"), tr("WAVs (*.wav)"), tr("OGGs (*.ogg)"), tr("MP3s (*.mp3)"), tr("Images (*.blp)"), tr("Shaders (*.bls)"), tr("DBCs (*.dbc)"), tr("DB2s (*.db2)"), tr("LUAs (*.lua)"), tr("XMLs (*.xml)"), tr("SKINs (*.skin)") };

public:
	FilterModes filterMode;

	dockWidgetFileList(QWidget *parent = 0);
	~dockWidgetFileList() {};

	void retranslate() { ui->retranslateUi(this); };
	void loadFiles();

private:
	Ui::dockWidget_FileList *ui;

	QString content = QString();
	QString filterString = QString();

	void addTreeChild(QTreeWidgetItem *parent, QString name);
};