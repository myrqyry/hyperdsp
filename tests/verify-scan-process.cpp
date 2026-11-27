#include "platform/scan_process.hpp"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace hyperdsp::platform;
using namespace std::chrono_literals;
int main(int argc,char** argv){
    if(argc!=2)return 2;
    ScanProcess process;
    auto wait=[&](){
        for(int i=0;i<200;++i){
            auto result=process.poll();
            if(result!=ScanResult::running)return result;
            std::this_thread::sleep_for(10ms);
        }
        return ScanResult::running;
    };
    if(process.start(argv[1],{"-c","import sys;sys.exit(0 if sys.argv[1]=='space here' else 7)","space here"})!=0 ||
       wait()!=ScanResult::success)return 3;
    if(process.start(argv[1],{"-c","raise SystemExit(7)"})!=0||wait()!=ScanResult::failed)return 4;
    if(process.start(argv[1],{"-c","import time;time.sleep(30)"})!=0||
       process.poll()!=ScanResult::running)return 5;
    if(process.start(argv[1],{"-c","raise SystemExit(0)"})==0)return 6;
    process.stop();
    if(process.poll()!=ScanResult::idle)return 7;
    if(process.start(argv[1],{"-c","raise SystemExit(0)"})!=0||wait()!=ScanResult::success)return 8;
    std::puts("SCAN_PROCESS_PASS=1");
    return 0;
}
