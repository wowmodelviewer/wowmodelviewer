#include "CSVFile.h"
#include "logger/Logger.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include "Game.h"

CSVFile::CSVFile(std::string file) : m_file(std::move(file))
{
}

static std::string toLower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
	return s;
}

static std::vector<std::string> splitString(const std::string& s, char delimiter)
{
	std::vector<std::string> tokens;
	std::istringstream stream(s);
	std::string token;
	while (std::getline(stream, token, delimiter))
		tokens.push_back(token);
	return tokens;
}

bool CSVFile::open()
{
	std::ifstream file(core::Game::instance().configFolder() + m_file);
	if (!file.is_open())
	{
		LOG_ERROR << "Fail to open" << m_file;
		return false;
	}

	// read first line to gather fields' position
	std::string headerLine;
	if (!std::getline(file, headerLine))
		return false;

	m_fields = splitString(toLower(headerLine), ';');

	std::string line;
	while (std::getline(file, line))
	{
		std::vector<std::string> vals = splitString(line, ';');
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
		unsigned int fieldIndex = 0;

		for (; fieldIndex < m_fields.size(); fieldIndex++)
			if (toLower(it->name) == m_fields[fieldIndex])
				break;

		if (fieldIndex >= m_fields.size())
		{
			result.emplace_back();
			continue;
		}

		result.push_back(m_values[recordIndex][fieldIndex]);
	}

	return result;
}
