#pragma once
#include <string>
#include <cstdlib>
class String {
 public:
  std::string s;
  String() {}
  String(const char *c) : s(c) {}
  String(const std::string &x) : s(x) {}
  String &operator+=(char c) { s += c; return *this; }
  bool endsWith(const char *t) const { std::string x(t); return s.size() >= x.size() && s.compare(s.size() - x.size(), x.size(), x) == 0; }
  unsigned int length() const { return s.size(); }
  char operator[](unsigned int i) const { return s[i]; }
  bool operator==(const char *c) const { return s == c; }
  int indexOf(const char *t) const { auto p = s.find(t); return p == std::string::npos ? -1 : (int)p; }
  int indexOf(const char *t, int from) const { auto p = s.find(t, from); return p == std::string::npos ? -1 : (int)p; }
  const char *c_str() const { return s.c_str(); }
  int indexOf(char c) const { auto p = s.find(c); return p == std::string::npos ? -1 : (int)p; }
  int indexOf(char c, int from) const { auto p = s.find(c, from); return p == std::string::npos ? -1 : (int)p; }
  String substring(int from) const { return String(s.substr(from)); }
  float toFloat() const { return std::strtof(s.c_str(), nullptr); }
};
