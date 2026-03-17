#pragma once

#include <fstream>
#include <map>
#include <string>

namespace core
{
	class IniFile
	{
	public:
		explicit IniFile(const std::string& path) : m_path(path)
		{
			load();
		}

		explicit IniFile(const std::wstring& path) : m_wpath(path)
		{
			load();
		}

		std::string getString(const std::string& key, const std::string& defaultValue = "") const
		{
			const auto it = m_data.find(key);
			if (it == m_data.end())
				return defaultValue;

			// Strip surrounding double quotes (QSettings backward compatibility)
			std::string val = it->second;
			if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
				val = val.substr(1, val.size() - 2);
			return val;
		}

		int getInt(const std::string& key, int defaultValue = 0) const
		{
			const auto it = m_data.find(key);
			if (it == m_data.end())
				return defaultValue;
			try { return std::stoi(it->second); }
			catch (...) { return defaultValue; }
		}

		double getDouble(const std::string& key, double defaultValue = 0.0) const
		{
			const auto it = m_data.find(key);
			if (it == m_data.end())
				return defaultValue;
			try { return std::stod(it->second); }
			catch (...) { return defaultValue; }
		}

		bool getBool(const std::string& key, bool defaultValue = false) const
		{
			const auto it = m_data.find(key);
			if (it == m_data.end())
				return defaultValue;
			const std::string& v = it->second;
			if (v == "true" || v == "1") return true;
			if (v == "false" || v == "0") return false;
			return defaultValue;
		}

		std::wstring getWString(const std::string& key, const std::wstring& defaultValue = L"") const
		{
			const std::string s = getString(key);
			if (s.empty())
				return defaultValue;
			// Convert UTF-8 string to wide string
			const int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
			if (len == 0)
				return defaultValue;
			std::wstring result(len, L'\0');
			MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), result.data(), len);
			return result;
		}

		void setValue(const std::string& key, int value)
		{
			m_data[key] = std::to_string(value);
		}

		void setValue(const std::string& key, double value)
		{
			m_data[key] = std::to_string(value);
		}

		void setValue(const std::string& key, bool value)
		{
			m_data[key] = value ? "true" : "false";
		}

		void setValue(const std::string& key, const std::string& value)
		{
			m_data[key] = value;
		}

		void setValue(const std::string& key, const std::wstring& value)
		{
			// Convert wide string to UTF-8
			if (value.empty())
			{
				m_data[key] = "";
				return;
			}
			const int len = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
			                                    nullptr, 0, nullptr, nullptr);
			std::string result(len, '\0');
			WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
			                    result.data(), len, nullptr, nullptr);
			m_data[key] = result;
		}

		void sync() const
		{
			std::ofstream file;
			if (!m_wpath.empty())
				file.open(m_wpath);
			else
				file.open(m_path);

			if (!file.is_open())
				return;

			// Group keys by section and write in order
			std::map<std::string, std::map<std::string, std::string>> sections;
			for (const auto& [fullKey, value] : m_data)
			{
				const auto pos = fullKey.find('/');
				if (pos != std::string::npos)
					sections[fullKey.substr(0, pos)][fullKey.substr(pos + 1)] = value;
				else
					sections["General"][fullKey] = value;
			}

			bool first = true;
			for (const auto& [section, keys] : sections)
			{
				if (!first)
					file << '\n';
				first = false;
				file << '[' << section << "]\n";
				for (const auto& [key, value] : keys)
					file << key << '=' << value << '\n';
			}
		}

	private:
		void load()
		{
			std::ifstream file;
			if (!m_wpath.empty())
				file.open(m_wpath);
			else
				file.open(m_path);

			if (!file.is_open())
				return;

			std::string currentSection;
			std::string line;
			while (std::getline(file, line))
			{
				// Trim trailing \r for Windows line endings
				if (!line.empty() && line.back() == '\r')
					line.pop_back();

				// Skip empty lines and comments
				if (line.empty() || line[0] == ';' || line[0] == '#')
					continue;

				// Section header
				if (line.front() == '[' && line.back() == ']')
				{
					currentSection = line.substr(1, line.size() - 2);
					continue;
				}

				// Key=Value
				const auto eq = line.find('=');
				if (eq == std::string::npos)
					continue;

				std::string key = line.substr(0, eq);
				std::string value = line.substr(eq + 1);

				// Skip QSettings @variant entries (binary blobs we can't parse)
				if (!value.empty() && value[0] == '@')
					continue;

				if (!currentSection.empty())
					key = currentSection + "/" + key;

				m_data[key] = value;
			}
		}

		std::string m_path;
		std::wstring m_wpath;
		std::map<std::string, std::string> m_data;
	};
}
