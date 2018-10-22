#include "dockWidget_FileList.h"
#include "Game.h"
#include <set>
#include <qdebug.h>

FileTreeModel::FileTreeModel(QObject * parent)
	: QAbstractItemModel(parent)
{
	
}

FileTreeModel::~FileTreeModel()
{
}

int FileTreeModel::rowCount(const QModelIndex & parent) const
{
	return items.count();
}

QVariant FileTreeModel::data(const QModelIndex & index, int role) const
{
	if (!index.isValid())
		return QVariant();

	if (index.row() >= items.count())
		return QVariant();

	if (role == Qt::DisplayRole)
		return items.at(index.row())->name;
	else
		return QVariant();
}

QVariant FileTreeModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	return QStringLiteral("Filename");
}

int FileTreeModel::treeContains(QList<treeItem*> itemList, QString name)
{
	for (uint32_t i = 0; i < (uint32_t)itemList.count(); i++)
	{
		if (itemList.at(i)->name == name)
			return i;
	}
	return -1;
}

void FileTreeModel::addToTree(QString name)
{
	QStringList path = name.split("/");
	int nameRoot = treeContains(items, path.at(0));

	if (nameRoot == -1)
	{
		treeItem *a = new treeItem;
		a->name = path.at(0);
		items.push_back(a);
		nameRoot = items.count() - 1;
	}

	for (int i = 1; i < path.size(); i++)
	{
		treeItem* parent = items.at(nameRoot);
		if (treeContains(parent->children, path.at(i)) == false)
		{
			treeItem *a = new treeItem;
			a->name = path.at(i);
			parent->children.push_back(a);
			nameRoot = parent->children.count() - 1;
		}
	}
}

dockWidgetFileList::dockWidgetFileList(QWidget *parent)
	: QWidget(parent), ui(new Ui::dockWidget_FileList)
{
	qInfo("Initializing File List Dock Widget...");
	ui->setupUi(this);

	filterMode = FILE_FILTER_MODEL;
}

void dockWidgetFileList::loadFiles()
{
	content = ui->searchBar->text().toLower().trimmed();
	filterString = "^.*" + content + ".*\\." + filterStrings[filterMode];

	std::set<GameFile *> files;
	GAMEDIRECTORY.getFilteredFiles(files, filterString);

	qInfo() << "Initializing File Controls - Filtering done - files found:" << files.size();

	ui->treeWidget->clear();
	ui->treeWidget->setColumnCount(1);

	FileTreeModel* treeModel;

	for (std::set<GameFile *>::iterator it = files.begin(); it != files.end(); ++it)
	{
		QString name = (*it)->fullname();
		qDebug() << "Original Filename:" << name;

		return;

		treeModel->addToTree(name);
	}


}

void dockWidgetFileList::addTreeChild(QTreeWidgetItem * parent, QString name)
{
	QTreeWidgetItem *item = new QTreeWidgetItem();

	item->setText(0, name);
	parent->addChild(item);
}
