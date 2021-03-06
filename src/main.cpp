#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <poll.h>
#include <string>
#include <sys/inotify.h>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>

#include "config.h"

template <class... Args>
void print(Args... args)
{
	(std::cout << ... << args) << std::endl;
}

template <class... Args>
void debug_print(Args... args)
{
	if(std::string(BUILD_TYPE) == "Debug") (std::cout << ... << args) << std::endl;
}

template <class... Args>
void error(int exit_code, Args... args)
{
	std::cerr << "ERROR: ";
	(std::cerr << ... << args);
	exit(exit_code);
}

int main(int argc, char **argv)
{
	debug_print("Debug output enabled");
	print("Project: ", PROJ_NAME);
	print("Version: ", PROJ_VERSION);
	print("Link: ", PROJ_LINK);

	std::string watch_file;
	if(argc < 2)
	{
		debug_print("watch file not provided, trying to guess");
		std::filesystem::directory_entry userdata("/home/gray/.local/share/Steam/userdata");
		std::filesystem::path path;
		int counter = 0;

		for(const auto& entry : std::filesystem::directory_iterator(userdata))
		{
			if(entry.path().filename() != "anonymous")
			{
				if(counter == 0)
				{
					++counter;
					path = entry.path().filename();
				}
				else error(7, "More than one userdata dir detected, can't autodecide");
			}
		}

		if(!path.empty())
		{
			path += "730/local/cfg/.restart-watch.cfg";
			auto cfg = std::filesystem::directory_entry(std::filesystem::absolute(path));
			debug_print("directory_entry: ", cfg);
			if(cfg.is_regular_file()) watch_file = cfg.path().u8string();
		}
		else error(8, "Couldn't autodetect CSGO userdata folder");
	}
	else
	{
		watch_file = argv[1];
	}

	debug_print("Filename: ", watch_file);
	std::exit(1);

	while(true)
	{
		int pidfd = -1;
		{
			pid_t pid;
			while(true)
			{
				FILE *cmd = popen("pidof csgo_linux64", "r");
	
				// buffer size of 10 should be enough
				char pid_c[10];
				std::fgets(pid_c, 10, cmd);
	
				try
				{
					pid = std::stoi(pid_c);
				}
				catch(const std::invalid_argument&)
				{
					debug_print("csgo_linux64 process not found, waiting");
					std::this_thread::sleep_for(std::chrono::seconds(2));
					continue;
				}
				pclose(cmd);
				break;
			}
			debug_print("CSGO PID: ", pid);
	
			pidfd = syscall(SYS_pidfd_open, pid, 0);
			if(pidfd == -1) error(4, "pidfd_open() failed");
			else debug_print("Successfully got a pidfd: ", pidfd);
		}
	
		int inotfd = -1;
		{
			inotfd = inotify_init();
			if(inotfd == -1) error(1, "failed to create an inotify file descriptor");
			else debug_print("Successfully created an inotify file descriptor: ", inotfd); 
	
			int wd = inotify_add_watch(inotfd, watch_file.c_str(), IN_MODIFY);
			if(wd == -1) error(2, "failed to create a watch descriptor");
			else debug_print("Successfully created a watch descriptor: ",  wd); 
		}
	
		struct pollfd polls[2];
	
		polls[0].fd = inotfd;
		polls[0].events = POLLIN;
	
		polls[1].fd = pidfd;
		polls[1].events = POLLIN;
	
		debug_print("polls[0].fd = ", pidfd, '\n', "polls[1].fd = ", inotfd);
	
		bool restart_requested = false;
		while(true)
		{
			int polled_num = poll(polls, 2, -1);
	
			if(polled_num == -1) error(6, "poll() failed");
			if(polled_num > 0)
			{
				if(polls[0].revents & POLLIN)
				{
					char buf[1024];
					read(inotfd, buf, 1024);
					print("watched file modified, restart requested");
					restart_requested = true;
				}
				if(polls[1].revents & POLLIN)
				{
					debug_print("csgo has been closed");
					if(restart_requested)
					{
						print("restarting csgo");
						std::system("xdg-open steam://run/730");
						break;
					}
					else
					{
						print("exiting");
						return 0;
					}
				}
			}
		}
	}

	return 0;
}
