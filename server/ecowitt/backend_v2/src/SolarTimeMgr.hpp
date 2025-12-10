//
//  SolarTimeMgr.hpp

//
//  Created by Vincent Moscaritolo on 5/26/21.
//

#ifndef SolarTimeMgr_hpp
#define SolarTimeMgr_hpp

#include <stdio.h>
#include <time.h>
#include <string>
#include <stdint.h>

#include <sys/time.h>
 
/* Useful Constants */
#define SECS_PER_MIN  ((time_t)(60UL))
#define SECS_PER_HOUR ((time_t)(3600UL))
#define SECS_PER_DAY  ((time_t)(SECS_PER_HOUR * 24UL))
#define DAYS_PER_WEEK ((time_t)(7UL))
#define SECS_PER_WEEK ((time_t)(SECS_PER_DAY * DAYS_PER_WEEK))
#define SECS_PER_YEAR ((time_t)(SECS_PER_DAY * 365UL)) // TODO: ought to handle leap years
#define SECS_YR_2000  ((time_t)(946684800UL)) // the time at the start of y2k
 
/* Useful Macros for getting elapsed time */
#define numberOfSeconds(_time_) ((_time_) % SECS_PER_MIN)
#define numberOfMinutes(_time_) (((_time_) / SECS_PER_MIN) % SECS_PER_MIN)
#define numberOfHours(_time_) (((_time_) % SECS_PER_HOUR) / SECS_PER_HOUR)
#define dayOfWeek(_time_) ((((_time_) / SECS_PER_DAY + 4)  % DAYS_PER_WEEK)+1) // 1 = Sunday
#define elapsedDays(_time_) ((_time_) / SECS_PER_DAY)  // this is number of days since Jan 1 1970
#define elapsedSecsToday(_time_) ((_time_) % SECS_PER_DAY)   // the number of seconds since last midnight
// The following macros are used in calculating alarms and assume the clock is set to a date later than Jan 1 1971
// Always set the correct time before setting alarms
#define previousMidnight(_time_) (((_time_) / SECS_PER_DAY) * SECS_PER_DAY)  // time at the start of the given day
#define nextMidnight(_time_) (previousMidnight(_time_)  + SECS_PER_DAY)   // time at the end of the given day
#define elapsedSecsThisWeek(_time_) (elapsedSecsToday(_time_) +  ((dayOfWeek(_time_)-1) * SECS_PER_DAY))   // note that week starts on day 1
#define previousSunday(_time_) ((_time_) - elapsedSecsThisWeek(_time_))      // time at the start of the week for the given time
#define nextSunday(_time_) (previousSunday(_time_)+SECS_PER_WEEK)          // time at the end of the week for the given time

#define nextInterval(_time_, _minutesRange_)  (((_time_) / (_minutesRange_ * SECS_PER_MIN) * (_minutesRange_ * SECS_PER_MIN)) + (_minutesRange_ * SECS_PER_MIN))

#include "sunset.h"
 
typedef struct {
    bool    isValid;
	double 	sunSetMins;
	double 	sunriseMins;
	double 	civilSunSetMins;
	double 	civilSunRiseMins;
    
 	long	gmtOffset;				//  offset from UTC in seconds for previousMidnight
 	time_t 	previousMidnight;		//  according to LocalTime
 
    uint8_t  moonPhase;
    double  moonVisable;
    std::string  moonPhaseName;
    
	double		longitude;
	double 		latitude;
	std::string 	timeZoneString;
 	long 			upTime;
	
} solarTimes_t;


class SolarTimeMgr {

	public:
 
	static SolarTimeMgr *shared() {
		if(!sharedInstance){
			sharedInstance = new SolarTimeMgr;
		}
		return sharedInstance;
	}

//	friend InsteonMgr;

	SolarTimeMgr();

	void setLatLong(double latitude, double longitude);
	bool getSolarEventTimes(solarTimes_t& events);
	bool calculateSolarEventTimes();
 
	long upTime();
	
private:
	
	static SolarTimeMgr *sharedInstance;


	SunSet 			_sun;

	solarTimes_t _cachedSolar;
	double		 _longitude;
	double 		 _latitude;
	time_t 		_startTime;		// to calculate uptime

};

#endif /* SolarTimeMgr_hpp */
