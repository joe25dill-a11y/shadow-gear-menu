#pragma once

#include <string>

bool ValueScanActive();
size_t ValueScanCount();
bool ValueScanFirst(const std::wstring& text, bool whole_number, std::wstring& error);
bool ValueScanNext(const std::wstring& text, bool whole_number, std::wstring& error);
bool ValueScanWrite(const std::wstring& text, bool whole_number, std::wstring& error);
void ValueScanClear();
