#include "CSVFile.h"
#include "logger/Logger.h"
#include <QFile>
#include "Game.h"

CSVFile::CSVFile(QString file) : m_file(std::move(file))
{
}

bool CSVFile::open()
{
	QFile file(core::Game::instance().configFolder() + m_file);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
	{
		LOG_ERROR << "Fail to open" << m_file;
		return false;
	}

	QTextStream in(&file);

	// read first line to gather fields' position
	QStringList list = in.readLine().toLower().split(";");

	for (auto it : list)
		m_fields.push_back(it);

	while (!in.atEnd())
	{
		QStringList List = in.readLine().split(";");

		std::vector<std::string> vals;

		for (auto it : List)
			vals.push_back(it.toStdString());

		m_values.push_back(vals);

		recordCount++;
	}

	return true;
}

bool CSVFile::close()
{
	return true;
}

CSVFile::~CSVFile()
{
	CSVFile::close();
}

std::vector<std::string> CSVFile::get(unsigned int recordIndex, const core::TableStructure* structure) const
{
	std::vector<std::string> result;

	for (const auto it : structure->fields)
	{
		uint fieldIndex = 0;

		for (; fieldIndex < m_fields.size(); fieldIndex++)
			if (it->name.toLower() == m_fields[fieldIndex])
				break;

		result.push_back(m_values[recordIndex][fieldIndex]);
	}

	return result;
}
