#pragma once
#include "native_library.hpp"
#include <vector>
#include <cerrno>
#ifdef _WIN32
#else
#include <csignal>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <crt_externs.h>
#else
extern char **environ;
#endif
#endif

namespace hyperdsp::platform {
enum class ScanResult {idle,running,success,failed};

// Own one scanner process and its descendants. No shell or audio-thread work.
class ScanProcess {
#ifdef _WIN32
    HANDLE process_=nullptr,job_=nullptr;
    static std::wstring quote(const std::wstring& arg){
        std::wstring result=L"\"";
        size_t slashes=0;
        for(wchar_t c:arg){
            if(c==L'\\'){++slashes;continue;}
            result.append(c==L'"'?2*slashes+1:slashes,L'\\');
            slashes=0;
            result.push_back(c);
        }
        result.append(2*slashes,L'\\');
        result.push_back(L'"');
        return result;
    }
    static std::filesystem::path executable(const std::string& program){
        auto requested=native_path(program);
        if(requested.is_absolute())return requested;
        if(requested.has_parent_path())return {};
        // Search only explicit absolute PATH entries, not the current directory.
        if(requested.extension().empty())requested+=L".exe";
        const DWORD size=GetEnvironmentVariableW(L"PATH",nullptr,0);
        if(!size)return {};
        std::wstring path(size,L'\0');
        const DWORD length=GetEnvironmentVariableW(L"PATH",path.data(),size);
        if(!length||length>=size)return {};
        path.resize(length);
        for(size_t pos=0;pos<=path.size();){
            const auto end=path.find(L';',pos);
            auto part=path.substr(pos,end==path.npos?path.npos:end-pos);
            if(part.size()>=2&&part.front()==L'"'&&part.back()==L'"')part=part.substr(1,part.size()-2);
            std::filesystem::path root(part);
            std::error_code ec;
            if(root.is_absolute()&&std::filesystem::is_regular_file(root/requested,ec))return root/requested;
            if(end==path.npos)break;
            pos=end+1;
        }
        return {};
    }
    void release(){
        if(process_)CloseHandle(process_);
        if(job_)CloseHandle(job_);
        process_=job_=nullptr;
    }
#else
    pid_t child_=-1;
#endif
public:
    ScanProcess()=default;
    ScanProcess(const ScanProcess&)=delete;
    ScanProcess& operator=(const ScanProcess&)=delete;
    ~ScanProcess(){stop();}
    int start(const std::string& program,std::vector<std::string> args){
#ifdef _WIN32
        if(process_)return ERROR_BUSY;
        auto exe=executable(program);
        if(exe.empty())return ERROR_FILE_NOT_FOUND;
        std::wstring cmd=quote(exe.native());
        for(auto& arg:args){cmd+=L' ';cmd+=quote(native_path(arg).native());}
        job_=CreateJobObjectW(nullptr,nullptr);
        if(!job_)return int(GetLastError());
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};
        limit.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(!SetInformationJobObject(job_,JobObjectExtendedLimitInformation,&limit,sizeof(limit))){
            auto err=GetLastError();release();return int(err);
        }
        STARTUPINFOW startup{};startup.cb=sizeof(startup);
        PROCESS_INFORMATION info{};
        if(!CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,FALSE,
            CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,nullptr,&startup,&info)){
            auto err=GetLastError();release();return int(err);
        }
        process_=info.hProcess;
        if(!AssignProcessToJobObject(job_,process_)||ResumeThread(info.hThread)==DWORD(-1)){
            auto err=GetLastError();
            TerminateProcess(process_,1);
            WaitForSingleObject(process_,INFINITE);
            CloseHandle(info.hThread);release();return int(err);
        }
        CloseHandle(info.hThread);
        return 0;
#else
        if(child_>0)return EBUSY;
        args.insert(args.begin(),program);
        std::vector<char*> argv;
        for(auto& arg:args)argv.push_back(arg.data());
        argv.push_back(nullptr);
        posix_spawnattr_t attr;
        int rc=posix_spawnattr_init(&attr);
        if(rc)return rc;
        rc=posix_spawnattr_setpgroup(&attr,0);
        if(!rc)rc=posix_spawnattr_setflags(&attr,POSIX_SPAWN_SETPGROUP);
        pid_t pid=-1;
#ifdef __APPLE__
        char** environment=*_NSGetEnviron();
#else
        char** environment=environ;
#endif
        if(!rc)rc=posix_spawnp(&pid,program.c_str(),nullptr,&attr,argv.data(),environment);
        posix_spawnattr_destroy(&attr);
        if(!rc)child_=pid;
        return rc;
#endif
    }
    ScanResult poll(){
#ifdef _WIN32
        if(!process_)return ScanResult::idle;
        const auto wait=WaitForSingleObject(process_,0);
        if(wait==WAIT_TIMEOUT)return ScanResult::running;
        DWORD code=1;
        const bool ok=wait==WAIT_OBJECT_0&&GetExitCodeProcess(process_,&code)&&code==0;
        release();
        return ok?ScanResult::success:ScanResult::failed;
#else
        if(child_<=0)return ScanResult::idle;
        int status=0;pid_t result;
        do{result=waitpid(child_,&status,WNOHANG);}while(result<0&&errno==EINTR);
        if(result==0)return ScanResult::running;
        const bool ok=result==child_&&WIFEXITED(status)&&WEXITSTATUS(status)==0;
        child_=-1;
        return ok?ScanResult::success:ScanResult::failed;
#endif
    }
    void stop(){
#ifdef _WIN32
        if(!process_)return;
        if(!TerminateJobObject(job_,1))TerminateProcess(process_,1);
        WaitForSingleObject(process_,INFINITE);
        release();
#else
        if(child_<=0)return;
        // Reap first to avoid signaling a process group after its leader exited.
        if(poll()!=ScanResult::running)return;
        kill(-child_,SIGKILL);
        int status;
        while(waitpid(child_,&status,0)<0&&errno==EINTR){}
        child_=-1;
#endif
    }
};
}
