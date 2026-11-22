#include "veyra/engine/ColorLookStore.h"
#include <windows.h>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
namespace veyra::engine {
namespace {
std::string utf8(const std::wstring& text){
    const int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0,nullptr,nullptr);
    std::string result(size_t(std::max(0,size)),0);
    if(size>0)WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),int(text.size()),result.data(),size,nullptr,nullptr);
    return result;
}
std::wstring wide(const std::string& text){
    const int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0);
    std::wstring result(size_t(std::max(0,size)),0);
    if(size>0)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),result.data(),size);
    return result;
}
bool nameOk(const std::wstring& name){
    if(name.empty()||name.size()>48)return false;
    if(name.front()==L' '||name.back()==L' ')return false;
    return std::none_of(name.begin(),name.end(),[](wchar_t c){return c<32;});
}
}
ColorLookStore::ColorLookStore(std::filesystem::path dataDirectory):path_(std::move(dataDirectory)/"color-looks.v1"){}
bool ColorLookStore::load(){
    entries_.clear();corrupt_=false;error_.clear();
    std::error_code ec;
    if(!std::filesystem::exists(path_,ec))return true;
    if(std::filesystem::file_size(path_,ec)>65536){corrupt_=true;error_=L"The color preset file is larger than 64 KiB; the original file is kept";return false;}
    std::ifstream file(path_,std::ios::binary);
    if(!file){corrupt_=true;error_=L"The color preset file cannot be read";return false;}
    std::string data((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
    std::istringstream stream(data);
    stream.imbue(std::locale::classic());
    std::string magic;int version=0;
    if(!(stream>>magic>>version)||magic!="VEYRA_COLOR_LOOKS"||version<1||version>2){corrupt_=true;error_=L"The color preset file is damaged or an unsupported version; the original file is kept";return false;}
    std::vector<ColorLook> loaded;
    for(;;){
        std::string name;
        if(!(stream>>std::quoted(name)))break;
        ColorLook look;look.name=wide(name);
        std::string lut;
        // Version 1 predates the per-section bypass mask (schema 18).
        if(!readColorSettings(stream,look.color,lut,version>=2?19:18)){corrupt_=true;error_=L"The color preset file is damaged; the original file is kept";return false;}
        if(!lut.empty()&&!look.color.setLutName(wide(lut))){corrupt_=true;error_=L"The LUT name in the color preset is invalid; the original file is kept";return false;}
        if(!nameOk(look.name)){corrupt_=true;error_=L"Invalid color preset name; the original file is kept";return false;}
        loaded.push_back(std::move(look));
    }
    stream>>std::ws;
    if(!stream.eof()||loaded.size()>64){corrupt_=true;error_=L"The color preset file has leftover data at the end; the original file is kept";return false;}
    entries_=std::move(loaded);
    return true;
}
bool ColorLookStore::save(){
    if(corrupt_){error_=L"The preset file is damaged; overwrite refused";return false;}
    std::error_code ec;
    std::filesystem::create_directories(path_.parent_path(),ec);
    if(ec){error_=L"Could not create the preset folder";return false;}
    std::ostringstream out;
    out.imbue(std::locale::classic());out<<std::setprecision(std::numeric_limits<float>::max_digits10);
    out<<"VEYRA_COLOR_LOOKS 2\n";
    for(const auto& look:entries_){
        out<<std::quoted(utf8(look.name))<<' ';
        writeColorSettings(out,look.color,utf8(look.color.lutNameString()));
        out<<'\n';
    }
    const auto data=out.str();
    if(data.size()>65536){error_=L"The color preset is larger than 64 KiB";return false;}
    auto temporary=path_;temporary+=L".tmp-"+std::to_wstring(GetCurrentProcessId());
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE){error_=L"Could not create the preset temporary file";return false;}
    DWORD written=0;
    bool ok=WriteFile(file,data.data(),DWORD(data.size()),&written,nullptr)&&written==data.size()&&FlushFileBuffers(file);
    CloseHandle(file);
    ok=ok&&MoveFileExW(temporary.c_str(),path_.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!ok){DeleteFileW(temporary.c_str());error_=L"Atomic save of the color preset failed; the original file was not replaced";return false;}
    error_.clear();
    return true;
}
bool ColorLookStore::put(std::wstring name,const ColorSettings& colour,bool replace){
    if(!nameOk(name)||!colour.validate().empty()){error_=L"Invalid preset name or settings";return false;}
    const auto old=entries_;
    const auto existing=std::find_if(entries_.begin(),entries_.end(),[&](const ColorLook& look){return look.name==name;});
    if(existing!=entries_.end()){
        if(!replace){error_=L"A color preset with this name already exists";return false;}
        existing->color=colour;
    }else{
        if(entries_.size()>=64){error_=L"At most 64 color presets can be saved";return false;}
        entries_.push_back({std::move(name),colour});
    }
    if(save())return true;
    entries_=old;
    return false;
}
bool ColorLookStore::erase(size_t index){
    if(index>=entries_.size())return false;
    const auto old=entries_;
    entries_.erase(entries_.begin()+std::ptrdiff_t(index));
    if(save())return true;
    entries_=old;
    return false;
}
bool ColorLookStore::exportFile(size_t index,const std::filesystem::path& target){
    if(index>=entries_.size()){error_=L"No color preset selected";return false;}
    const auto& look=entries_[index];
    std::ostringstream out;
    out.imbue(std::locale::classic());out<<std::setprecision(std::numeric_limits<float>::max_digits10);
    out<<"VEYRA_COLOR_LOOK 2\n"<<std::quoted(utf8(look.name))<<' ';
    writeColorSettings(out,look.color,utf8(look.color.lutNameString()));
    out<<'\n';
    const auto data=out.str();
    HANDLE file=CreateFileW(target.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE){error_=L"Could not write the export file";return false;}
    DWORD written=0;
    const bool ok=WriteFile(file,data.data(),DWORD(data.size()),&written,nullptr)&&written==data.size();
    CloseHandle(file);
    if(!ok)error_=L"Could not write the export file";
    return ok;
}
bool ColorLookStore::importFile(const std::filesystem::path& source,std::wstring& nameOut){
    std::ifstream file(source,std::ios::binary);
    if(!file){error_=L"Could not read the imported file";return false;}
    std::string data((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
    std::istringstream stream(data);
    stream.imbue(std::locale::classic());
    std::string magic,name;int version=0;
    if(!(stream>>magic>>version)||magic!="VEYRA_COLOR_LOOK"||version<1||version>2){error_=L"Not a valid .vpcolor file";return false;}
    if(!(stream>>std::quoted(name))){error_=L"The imported file has no preset name";return false;}
    ColorSettings colour;std::string lut;
    if(!readColorSettings(stream,colour,lut,version>=2?19:18)){error_=L"The settings block in the imported file is damaged";return false;}
    if(!lut.empty()&&!colour.setLutName(wide(lut))){error_=L"The LUT name in the imported file is invalid";return false;}
    stream>>std::ws;
    if(!stream.eof()){error_=L"The imported file has leftover data at the end";return false;}
    if(!put(wide(name),colour,true))return false;
    nameOut=wide(name);
    return true;
}
} // namespace veyra::engine
