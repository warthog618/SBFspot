/************************************************************************************************
	SBFspot - Yet another tool to read power production of SMA® solar inverters
	(c)2012-2015, SBF

	Latest version found at https://sbfspot.codeplex.com

	License: Attribution-NonCommercial-ShareAlike 3.0 Unported (CC BY-NC-SA 3.0)
	http://creativecommons.org/licenses/by-nc-sa/3.0/

	You are free:
		to Share — to copy, distribute and transmit the work
		to Remix — to adapt the work
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

	SMA is a registered trademark of SMA Solar Technology AG

************************************************************************************************/

/***********************************************************************************************
 ** SQLite is a software library that implements a self-contained, serverless, zero-configuration,
 ** transactional SQL database engine. SQLite is the most widely deployed SQL database engine in
 ** the world. The source code for SQLite is in the public domain.
 **
 ** Configuration for Windows build
 ** Visit http://www.sqlite.org/download.html
 **	Download sqlite-dll-win32-x86-3080403.zip (V3.8.4.3) (Precompiled Binaries for Windows)
 **	Extract sqlite3.dll to %windir%\System32
 **	Extract sqlite3.def to %ProgramFiles%\Microsoft Visual Studio 10.0\VC\lib\SQLite
 **	Start Visual Studio Command Prompt
 **	CD %ProgramFiles%\Microsoft Visual Studio 10.0\VC\lib\SQLite
 **	LIB.EXE /DEF:sqlite3.def /MACHINE:x86
 ** => this will create sqlite3.lib (needed by Linker)
 ** Download sqlite-amalgamation-3080403.zip (Source Code V3.8.4.3)
 ** Extract sqlite3.h to %ProgramFiles%\Microsoft Visual Studio 10.0\VC\include\SQLite
 ***********************************************************************************************/

#if defined(USE_SQLITE)

//TODO: MAX_INVERTERS is defined twice (Quick but dirty fix)
const int MAX_INVERTERS = 10;

#include "db_SQLite.h"
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>

using namespace std;

// Returns the columns for the query to get the extended values from SpotData.
// Values are fetched from the Config.
// Key is PVO-Vxx where xx identifies the extended field, from 5 thru 12.
// The value is the column name of the SpotData table to populate it with.
// e.g. PVO_V5=Temperature, PVO_V6=Uac1, PVO_V7=Pdc1
// The V5 and V6 are defaulted to the values above for backward compatibility.
// The non-NULL fetched columns are averaged in case multiple SpotData rows match.
// An empty string is returned in case of error.
string db_SQL_Base::get_pvo_extended_values()
{
    std::stringstream result;
    std::stringstream key;
    std::string value;
    for (int idx = 5; idx <= 12; idx++)
    {
        key.str("");
        key << "PVO_V" << idx;
        value.clear();
        if (get_config(key.str(),value) == SQLITE_OK) 
        {
            if (value.empty())
            {
                if (idx == 5)
                {
                    result << "avg(Temperature),";
                }
                else if (idx == 6)
                {
                    result << "avg(Uac1),";
                }
                else
                {
                    result << "NULL,";
                }
            }
            else
            {
                if (value == "NULL")
                {
                    result << "NULL,";
                }
                else
                {
                    result << "avg(" << value << "),";
                }
            }
        }
        else 
        {
	    print_error("get_config(" + key.str() + ") failed");
            return "";
        }
    }
    return result.str().substr(0, result.str().size()-1);
}

string db_SQL_Base::status_text(int status)
{
	switch (status)
	{
		//Grid Relay Status
		case 311: return "Open";
		case 51: return "Closed";

		//Device Status
		case 307: return "OK";
		case 455: return "Warning";
		case 35: return "Fault";

		//NaNStt=Information not available
		case 0xFFFFFD: return "N/A";

		default: return "?";
	}
}

int db_SQL_Base::open(string server, string user, string pass, string database)
{
	int result = SQLITE_OK;

	if (sqlite3_threadsafe() == 0)
	{
		print_error("SQLite3 libs are not threadsafe");
		return SQLITE_ERROR;
	}

	if (!m_dbHandle)	// Not yet open?
	{
		m_database = database;

		if (database.size() > 0)
			result = sqlite3_open_v2(database.c_str(), &m_dbHandle, SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX, NULL);
		else
			result = SQLITE_ERROR;

    	if(result == SQLITE_OK)
			sqlite3_busy_timeout(m_dbHandle, 2000);
		else
		{
			print_error("Can't open SQLite db [" + m_database + "]");
			m_dbHandle = NULL;
		}
	}

	return result;
}


int db_SQL_Base::close(void)
{
	int result = SQLITE_OK;

	if((result = sqlite3_close(m_dbHandle)) != SQLITE_OK)
        print_error("Can't close SQLite db [" + m_database + "]");
    else
	{
		m_database = "";
		m_dbHandle = NULL;
	}

	return result;
}

int db_SQL_Base::exec_query(string qry)
{
	int result = SQLITE_OK;
	int retrycount = 0;

	do
	{
		result = sqlite3_exec(m_dbHandle, qry.c_str(), NULL, NULL, NULL);
		if (result != SQLITE_OK)
		{
			print_error("sqlite3_exec() returned", qry);
			if (++retrycount > SQL_BUSY_RETRY_COUNT)
				break;
		}

	} while ((result == SQLITE_BUSY) || (result == SQLITE_LOCKED));

	if ((result == SQLITE_OK) && (retrycount > 0))
	{
		std::cout << "Query executed successfully after " << retrycount << " time" << (retrycount == 1 ? "" : "s") << std::endl;
	}

	return result;
}

int db_SQL_Base::type_label(InverterData *inverters[])
{
	std::stringstream sql;
	int rc = SQLITE_OK;

	for (int inv=0; inverters[inv]!=NULL && inv<MAX_INVERTERS; inv++)
	{
		sql.str("");

		// Instead of using REPLACE which is actually a DELETE followed by INSERT,
		// we do an INSERT OR IGNORE (for new records) followed by UPDATE (for existing records)
		sql << "INSERT OR IGNORE INTO Inverters VALUES(" <<
			inverters[inv]->Serial << ',' <<
			s_quoted(inverters[inv]->DeviceName) << ',' <<
			s_quoted(inverters[inv]->DeviceType) << ',' <<
			s_quoted(inverters[inv]->SWVersion) << ',' <<
			"0,0,0,0,0,0,'','',0)";

		if ((rc = exec_query(sql.str())) != SQLITE_OK)
			print_error("exec_query() returned", sql.str());

		sql.str("");

		sql << "UPDATE Inverters SET" <<
			" Name=" << s_quoted(inverters[inv]->DeviceName) <<
			",Type=" << s_quoted(inverters[inv]->DeviceType) <<
			",SW_Version=" << s_quoted(inverters[inv]->SWVersion) <<
			" WHERE Serial=" << inverters[inv]->Serial;

		if ((rc = exec_query(sql.str())) != SQLITE_OK)
			print_error("exec_query() returned", sql.str());
	}

	return rc;
}

int db_SQL_Base::device_status(InverterData *inverters[], time_t spottime)
{
	std::stringstream sql;
	int rc = SQLITE_OK;

	// Take time from computer instead of inverter
	//time_t spottime = cfg->SpotTimeSource == 0 ? inverters[0]->InverterDatetime : time(NULL);

	for (int inv=0; inverters[inv]!=NULL && inv<MAX_INVERTERS; inv++)
	{
		sql.str("");

		sql << "UPDATE Inverters SET" <<
			" TimeStamp=" << strftime_t(spottime) <<
			",TotalPac=" << (float)inverters[inv]->TotalPac <<
			",EToday=" << (double)inverters[inv]->EToday/1000 <<
			",ETotal=" << (double)inverters[inv]->ETotal/1000 <<
			",OperatingTime=" << (double)inverters[inv]->OperationTime/3600 <<
			",FeedInTime=" << (double)inverters[inv]->FeedInTime/3600 <<
			",Status=" << s_quoted(status_text(inverters[inv]->DeviceStatus)) <<
			",GridRelay=" << s_quoted(status_text(inverters[inv]->GridRelayStatus)) <<
			",Temperature=" << (float)inverters[inv]->Temperature/100 <<
			" WHERE Serial=" << inverters[inv]->Serial;

		if ((rc = exec_query(sql.str())) != SQLITE_OK)
			print_error("exec_query() returned", sql.str());
	}

	return rc;
}

int db_SQL_Base::batch_get_archdaydata(std::string &data, unsigned int Serial, int datelimit, int statuslimit, int& recordcount)
{
	std::stringstream sql;
	int rc = SQLITE_OK;
	recordcount = 0;

	sqlite3_stmt *dStmt = NULL;

        // Find time slots requiring upload
	sql << "SELECT datetime(TimeStamp, 'unixepoch', 'localtime') AS TimeSlot, "
            "strftime('%Y%m%d,%H:%M',datetime(TimeStamp,'unixepoch','localtime')) AS TimeString,"
            "TotalYield,Power FROM DayData WHERE "
            "PVoutput IS NULL "
            "AND Serial = " << Serial <<
            " AND TimeSlot > DATE(DATE(),'" << -(datelimit-2) << " day') "
            "ORDER BY TimeSlot "
            "LIMIT " << statuslimit;

        rc = sqlite3_prepare_v2(m_dbHandle, sql.str().c_str(), -1, &dStmt, NULL);

        if (dStmt == NULL)
        {
            return rc;
        }
        std::string extendedvalues = get_pvo_extended_values();
        if (extendedvalues.empty())
        {
	    print_error("failed to build extendedvalues");
	    return rc;
        }
        std::stringstream result;
        while (sqlite3_step(dStmt) == SQLITE_ROW)
        {
            // from 2nd record, add a record separator
            if (!data.empty())
            {
                result << ";";
            }
	    string timeslot = (char *)sqlite3_column_text(dStmt, 0);

            // Date and time
	    string dt = (char *)sqlite3_column_text(dStmt, 1);

            // Energy Generation
            int64_t V1 = sqlite3_column_int64(dStmt, 2);

            // Power Generation
            int64_t V2 = sqlite3_column_int64(dStmt, 3);

            // Mandatory values
            result << dt << "," << V1 << "," << V2;

	    sqlite3_stmt *cStmt = NULL;
            sql.str("");
            sql << "SELECT datetime((TimeStamp/300)*300,'unixepoch','localtime') AS TimeSlot,"
                "avg(EnergyUsed),avg(PowerUsed) FROM Consumption WHERE "
                "TimeSlot IS '" << timeslot << "' GROUP BY TimeSlot";

            rc = sqlite3_prepare_v2(m_dbHandle, sql.str().c_str(), -1, &cStmt, NULL);
            if (cStmt != NULL)
            {
                if (sqlite3_step(cStmt) == SQLITE_ROW)
                {
                    result << ",";
                    // Energy Consumption
                    if (sqlite3_column_type(cStmt, 1) != SQLITE_NULL)
                    {
                        result << sqlite3_column_int64(cStmt, 1);
                    }
                    result << ",";
                    // Power Consumption
                    if (sqlite3_column_type(cStmt, 2) != SQLITE_NULL)
                    {
                        result << sqlite3_column_int64(cStmt, 2);
                    }
                }
                else
                {
                    result << ",,";
                }
                sqlite3_finalize(cStmt);
            }
            else
            {
                result << ",,";
            }
	    sqlite3_stmt *sStmt = NULL;
            sql.str("");
            sql << "SELECT datetime((TimeStamp/300)*300,'unixepoch','localtime') AS TimeSlot," << extendedvalues <<
                " FROM SpotData WHERE Serial = " << Serial <<
                " AND TimeSlot IS '" << timeslot << "' GROUP BY TimeSlot";
            rc = sqlite3_prepare_v2(m_dbHandle, sql.str().c_str(), -1, &sStmt, NULL);
            if (sStmt != NULL)
            {
                if (sqlite3_step(sStmt) == SQLITE_ROW)
                {
                    // V5 to V12 - Temperature, Voltage and Extended values
                    for (int idx = 1; idx <= 8; idx++)
                    {
                        result << ",";
                        if (sqlite3_column_type(sStmt, idx) != SQLITE_NULL)
                        {
                            result << sqlite3_column_double(sStmt, idx);
                        }
                    }
                }
                sqlite3_finalize(sStmt);
            }
            const std::string& str = result.str();
            int end = str.length();
            // Ignore trailing commas...
            for (std::string::const_reverse_iterator it=str.rbegin(); it!=str.rend(); ++it, end--)
            {
                if ((*it) != ',')
                {
                    break;
                }
            }
            data.append(result.str().substr(0, end));
            recordcount++;
	    result.str("");
        }
        sqlite3_finalize(dStmt);
        return rc;
}

int db_SQL_Base::batch_set_pvoflag(const std::string &data, unsigned int Serial)
{
	std::stringstream sql;
	int rc = SQLITE_OK;

	vector<std::string> items;
	boost::split(items, data, boost::is_any_of(";"));

	sql << "UPDATE OR ROLLBACK DayData "
		"SET PVoutput=1 "
		"WHERE Serial=" << Serial << " "
		"AND strftime('%Y%m%d,%H:%M',datetime(TimeStamp, 'unixepoch', 'localtime')) "
		"IN (";

	bool firstitem = true;
	for (vector<std::string>::iterator it=items.begin(); it!=items.end(); ++it)
	{
		if (it->substr(15, 1) == "1")
		{
			if (!firstitem)
				sql << ",";
			else
				firstitem = false;
			sql << s_quoted(it->substr(0, 14));
		}
	}

	sql << ")";

	if ((rc = exec_query(sql.str())) != SQLITE_OK)
		print_error("exec_query() returned", sql.str());

	return rc;
}

int db_SQL_Base::set_config(const std::string key, const std::string value)
{
	std::stringstream sql;
	int rc = SQLITE_OK;

	sql << "INSERT OR REPLACE INTO Config (`Key`,`Value`) VALUES('" << key << "','" << value << "')";

	if ((rc = exec_query(sql.str())) != SQLITE_OK)
		print_error("exec_query() returned", sql.str());

	return rc;
}

int db_SQL_Base::get_config(const std::string key, std::string &value)
{
	std::stringstream sql;
	int rc = SQLITE_OK;

	sqlite3_stmt *pStmt = NULL;

	sql << "SELECT `Value` FROM Config WHERE `Key`='" << key << "'";

	rc = sqlite3_prepare_v2(m_dbHandle, sql.str().c_str(), -1, &pStmt, NULL);

	if (pStmt != NULL)
	{
		while (sqlite3_step(pStmt) == SQLITE_ROW)
		{
			char * text = (char *)sqlite3_column_text(pStmt, 0);
                        if (text != NULL)
                        {
                            value = text;
                        }
		}
		sqlite3_finalize(pStmt);
	}

	return rc;
}

int db_SQL_Base::get_config(const std::string key, int &value)
{
	int rc = SQLITE_OK;
	std::string strValue = intToString(value);
	if((rc = get_config(key, strValue)) == SQL_OK)
		value = boost::lexical_cast<int>(strValue);

	return rc;
}

std::string db_SQL_Base::timestamp(void)
{
    char buffer[100];
#if !defined WIN32
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm *tm;
    tm = localtime(&tv.tv_sec);

    snprintf(buffer, sizeof(buffer), "[%04d-%02d-%02d %02d:%02d:%02d.%03d] ",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, tm->tm_sec, (int)tv.tv_usec / 1000);
#else
    SYSTEMTIME time;
    ::GetLocalTime(&time);

    sprintf_s(buffer, sizeof(buffer), "[%04d-%02d-%02d %02d:%02d:%02d.%03d] ", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
#endif

	std::string sTimestamp(buffer);
	return sTimestamp;
}

#endif // #if defined(USE_SQLITE)
