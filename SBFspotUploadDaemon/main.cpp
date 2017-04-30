#define VERSION "1.0.1"
/************************************************************************************************
	SBFspot - Yet another tool to read power production of SMA solar inverters
	(c)2012-2014, SBF

	Latest version found at https://sbfspot.codeplex.com

	License: Attribution-NonCommercial-ShareAlike 3.0 Unported (CC BY-NC-SA 3.0)
	http://creativecommons.org/licenses/by-nc-sa/3.0/

	You are free:
		to Share � to copy, distribute and transmit the work
		to Remix � to adapt the work
	Under the following conditions:
	Attribution:
		You must attribute the work in the manner specified by the author or licensor
		(but not in any way that suggests that they endorse you or your use of the work).
	Noncommercial:
		You may not use this work for commercial purposes.
	Share Alike:
		If you alter, transform, or build upon this work, you may distribute the resulting work
		only under the same or similar license to this one.

DISCLAIMER:
	A user of SBFspot software acknowledges that he or she is receiving this
	software on an "as is" basis and the user is not relying on the accuracy
	or functionality of the software for any purpose. The user further
	acknowledges that any use of this software will be at his own risk
	and the copyright owner accepts no responsibility whatsoever arising from
	the use or application of the software.

************************************************************************************************/

// This project is based on "A simple C/C++ database daemon" and "Boost.org examples"
// See http://kahimyang.info/kauswagan/code-blogs/1326/a-simple-cc-database-daemon
// and http://www.boost.org/doc/libs/1_48_0/doc/html/boost_asio/example/fork/daemon.cpp

#include "../SBFspotUploadCommon/CommonServiceCode.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <syslog.h>

int quiet;
int verbose;

bool bStopping = false;
Configuration cfg;

// Signal handler
void handler(int signum)
{
    bStopping = true;
    syslog(LOG_INFO | LOG_USER, "Exit signal received.");
}

void pvo_upload(void)
{
	Log("Starting Daemon...", LOG_INFO_);

    CommonServiceCode();

	Log("Stopping Daemon...", LOG_INFO_);
}

int main(int argrc, char *argv[])
{
    int rc = 0;

	if (cfg.readSettings(argv[0], "") != Configuration::CFG_OK)
        exit(EXIT_FAILURE);

    // Check if log is writable
    if (Log("Starting SBFspotUploadDeamon Version " + std::string(VERSION), LOG_INFO_) != 0)
        exit(EXIT_FAILURE);

    // Check if DB is accessible
	db_SQL_Base db = db_SQL_Base();
	db.open(cfg.getSqlHostname(), cfg.getSqlUsername(), cfg.getSqlPassword(), cfg.getSqlDatabase());
	if (!db.isopen())
	{
        Log("Unable to open database. Check configuration.", LOG_ERROR_);
        exit(EXIT_FAILURE);
	}

    // Check DB Version
    int schema_version = 0;
    db.get_config(SQL_SCHEMAVERSION, schema_version);
    db.close();

    if (schema_version < SQL_MINIMUM_SCHEMA_VERSION)
	{
	    std::stringstream msg;
	    msg << "Upgrade your database to version " << SQL_MINIMUM_SCHEMA_VERSION;
        Log(msg.str().c_str(), LOG_ERROR_);
        exit(EXIT_FAILURE);
	}

    // Create child process
    pid_t pid = fork();

    if (pid < 0)
    {
        Log("Could not create child process.", LOG_ERROR_);
        exit(EXIT_FAILURE);
    }
    // Child process created, exit parent process
    if (pid > 0)
    {
        exit(EXIT_SUCCESS);
    }

    // Set file permission for files created by our child process
    umask(0);

    // Change to root directory to prevent mounted filesystem from being unmounted.
    if ((rc = chdir("/")) != 0)
        Log("Could not change directory.", LOG_WARNING_);

    // Create session for our new process
    pid_t sid = setsid();
    if (sid < 0)
    {
        Log("Could not create session for child process.", LOG_ERROR_);
        exit(EXIT_FAILURE);
    }

    // close all standard file descriptors.
    fclose(stdin);
    fclose(stdout);
    fclose(stderr);

    // We don't want the daemon to have any standard input.
    if (open("/dev/null", O_RDONLY) < 0)
    {
        Log("Unable to open /dev/null. See syslog for details.", LOG_ERROR_);
        syslog(LOG_ERR | LOG_USER, "Unable to open /dev/null: %m");
        exit(EXIT_FAILURE);
    }

    // Send standard output to a log file.
    const char* output = "/tmp/SBFspotUploadDaemon.out";
    const int flags = O_WRONLY | O_CREAT | O_APPEND;
    const mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    if (open(output, flags, mode) < 0)
    {
        Log("Unable to open output file. See syslog for details.", LOG_ERROR_);
        syslog(LOG_ERR | LOG_USER, "Unable to open output file %s: %m", output);
        exit(EXIT_FAILURE);
    }

    // Also send standard error to the same log file.
    if (dup(1) < 0)
    {
        Log("Unable to dup output descriptor. See syslog for details.", LOG_ERROR_);
        syslog(LOG_ERR | LOG_USER, "Unable to dup output descriptor: %m");
        exit(EXIT_FAILURE);
    }

    // Install our signal handler.
    // This responds to kill [pid] from the command line
    signal(SIGTERM, handler);

    // Ignore signal when terminal session is closed.
    // This keeps our daemon alive even when user closed terminal session
    signal(SIGHUP, SIG_IGN);

    // Start daemon loop
    pvo_upload();

    exit(EXIT_SUCCESS);
}

