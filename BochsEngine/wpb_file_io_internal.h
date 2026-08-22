#pragma once

#include <string>

void WPB_RegisterExternalFile(const std::string& path, Windows::Storage::Streams::IRandomAccessStream^ stream);
