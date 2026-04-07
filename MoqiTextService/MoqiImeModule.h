//
//	Copyright (C) 2013 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
//
//	This library is free software; you can redistribute it and/or
//	modify it under the terms of the GNU Library General Public
//	License as published by the Free Software Foundation; either
//	version 2 of the License, or (at your option) any later version.
//
//	This library is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//	Library General Public License for more details.
//
//	You should have received a copy of the GNU Library General Public
//	License along with this library; if not, write to the
//	Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
//	Boston, MA  02110-1301, USA.
//

#ifndef MOQI_IME_MODULE_H
#define MOQI_IME_MODULE_H

#include <LibIME2/src/ImeModule.h>
#include <string>
#include <vector>
#include <json/json.h>

namespace MoqiIME {

class MoqiImeModule : public Ime::ImeModule {
public:
	MoqiImeModule(HMODULE module);
	virtual ~MoqiImeModule(void);

	virtual Ime::TextService* createTextService();

	std::wstring& userDir () {
		return userDir_;
	}

	std::wstring& programDir() {
		return programDir_;
	}

	// called when config dialog needs to be launched
	virtual bool onConfigure(HWND hwndParent, LANGID langid, REFGUID rguidProfile);

	bool loadImeInfo(const std::string&, std::wstring& filePath, Json::Value& content);

	const std::vector<std::wstring>& backendDirs() {
		return backendDirs_;
	}

private:
	std::wstring userDir_;
	std::wstring programDir_;
	std::vector<std::wstring> backendDirs_;
};

} // namespace MoqiIME

#endif
