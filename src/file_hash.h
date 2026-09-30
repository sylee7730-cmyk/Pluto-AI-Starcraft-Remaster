#pragma once
#include <Windows.h>
#include <bcrypt.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

inline std::string file_sha256(const std::filesystem::path& path) {
  std::ifstream file(path,std::ios::binary);
  if(!file)throw std::runtime_error("Cannot open file for SHA-256 verification");
  BCRYPT_HASH_HANDLE hash=nullptr;
  if(BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE,&hash,nullptr,0,nullptr,0,0)<0)
    throw std::runtime_error("Cannot create SHA-256 hash");
  struct Cleanup {BCRYPT_HASH_HANDLE h;~Cleanup(){BCryptDestroyHash(h);}} cleanup{hash};
  std::array<char,65536> buffer;
  while(file) {
    file.read(buffer.data(),buffer.size());
    if(BCryptHashData(hash,reinterpret_cast<PUCHAR>(buffer.data()),static_cast<ULONG>(file.gcount()),0)<0)
      throw std::runtime_error("Cannot hash file");
  }
  if(!file.eof())throw std::runtime_error("Error while reading file for verification");
  std::array<unsigned char,32> digest{};
  if(BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)<0)
    throw std::runtime_error("Cannot finish SHA-256 hash");
  std::string result;const char* digits="0123456789abcdef";
  for(auto value:digest){result+=digits[value>>4];result+=digits[value&15];}
  return result;
}
inline constexpr char scr_sha256[]="32dbbdd001dd381cb1b3a719b7ad1fc918a9d4bc99661c675e00254efecca827";
inline constexpr char pluto_sha256[]="7e360b643c8c0156c03fe0cad9972a3058138ccfe22f921c5b4e0cd0aaf0abef";
