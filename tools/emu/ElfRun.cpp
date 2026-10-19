// Headless ELF runner: boots an ELF on Play!'s high-level BIOS emulation
// (no Sony BIOS needed) with a null GS, prints the program's TTY output and
// stops after N seconds.  usage: elfrun <file.elf> <seconds> [host dir]
#include <chrono>
#include <cstdio>
#include <thread>
#include "PS2VM.h"
#include "PS2VM_Preferences.h"
#include "AppConfig.h"
#include "gs/GSH_Null.h"
#include "iop/IopBios.h"
#include "StdStream.h"
#include <unistd.h>

fs::path CAppConfig::GetBasePath() const
{
	return fs::path("./play-data");
}

int main(int argc, const char** argv)
{
	if(argc < 3) { printf("usage: elfrun <file.elf> <seconds> [hostdir]\n"); return 1; }
	if(argc > 3) {
		CAppConfig::GetInstance().RegisterPreferencePath(PREF_PS2_HOST_DIRECTORY, argv[3]);
		CAppConfig::GetInstance().SetPreferencePath(PREF_PS2_HOST_DIRECTORY, argv[3]);
	}
	auto out = new Framework::CStdStream(stdout);
	bool over = false;
	CPS2VM vm;
	vm.Initialize();
	vm.CreateGSHandler(CGSH_Null::GetFactoryFunction());
	auto c = vm.m_ee->m_os->OnRequestExit.Connect([&over]() { over = true; });
	vm.m_ee->m_os->BootFromFile(argv[1]);
	dynamic_cast<CIopBios*>(vm.m_iop->m_bios.get())->GetIoman()->SetFileStream(Iop::CIoman::FID_STDOUT, out);
	vm.Resume();
	int seconds = atoi(argv[2]);
	for(int i = 0; i < seconds * 10 && !over; i++) std::this_thread::sleep_for(std::chrono::milliseconds(100));
	printf("\n[elfrun] %s after %d s\n", over ? "program exited" : "stopped", seconds);
	fflush(stdout);
	vm.Pause();
	_exit(0);
}
