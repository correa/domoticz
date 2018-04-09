// Tado plugin for Domoticz
//
// This plugin uses the same API as the my.tado.com web interface. Unfortunately this 
// API is unofficial and undocumented, but until Tado releases an official and 
// documented API, it's the best we've got.
//
// Main documentation for parts of the API can be found at 
// http://blog.scphillips.com/posts/2017/01/the-tado-api-v2/ but unfortunately 
// this information is slightly outdated, the authentication part in particular.


#include "stdafx.h"
#include "../main/Helper.h"
#include "../main/Logger.h"
#include "hardwaretypes.h"
#include "../main/localtime_r.h"
#include "../main/RFXtrx.h"
#include "../main/SQLHelper.h"
#include "../httpclient/HTTPClient.h"
#include "../httpclient/UrlEncode.h"
#include "../main/mainworker.h"
#include "../json/json.h"
#include "../webserver/Base64.h"
#include "Tado.h"
#include <regex>

#define round(a) ( int ) ( a + .5 )
const int TADO_POLL_INTERVAL = 30;  // The plugin should collect information from the API every n seconds.
const std::string TADO_API_ENVIRONMENT_URL = "https://my.tado.com/webapp/env.js";
const int TADO_TOKEN_MAXLOOPS = 12;	// Default token validity is 600 seconds before it needs to be refreshed.
									// Each cycle takes 30-35 seconds, so let's stay a bit on the safe side.

CTado::~CTado(void)
{
}

CTado::CTado(const int ID, const std::string &username, const std::string &password)
{
	m_HwdID = ID;
	m_TadoUsername = username;
	m_TadoPassword = password;

	_log.Log(LOG_TRACE, "Tado: Started Tado plugin with ID=" + boost::to_string(m_HwdID) + ", username=" + m_TadoUsername);

	Init();
}

bool CTado::StartHardware()
{
	_log.Log(LOG_NORM, "Tado: StartHardware() called.");
	Init();
	//Start worker thread
	m_thread = boost::shared_ptr<boost::thread>(new boost::thread(boost::bind(&CTado::Do_Work, this)));
	m_bIsStarted = true;
	sOnConnected(this);
	return (m_thread != NULL);
}

void CTado::Init()
{
	_log.Log(LOG_NORM, "Tado: Init() called.");
	m_stoprequested = false;
	m_bDoLogin = true;
	m_bDoGetHomes = true;
	m_bDoGetZones = false;
	m_bDoGetEnvironment = true;

	boost::trim(m_TadoUsername);
	boost::trim(m_TadoPassword);
}

bool CTado::StopHardware()
{
	_log.Log(LOG_NORM, "Tado: StopHardware() called.");
	if (m_thread != NULL)
	{
		assert(m_thread);
		m_stoprequested = true;
		m_thread->join();
	}
	m_bIsStarted = false;
	
	//if (!m_bDoLogin)
	//	Logout();

	return true;
}

bool CTado::WriteToHardware(const char * pdata, const unsigned char length)
{
	if (m_TadoAuthToken.size() == 0)
		return false;

	const tRBUF *pCmd = reinterpret_cast<const tRBUF *>(pdata);
	if (pCmd->LIGHTING2.packettype != pTypeLighting2)
		return false;

	int node_id = pCmd->LIGHTING2.id4;

	bool bIsOn = (pCmd->LIGHTING2.cmnd == light2_sOn);


	int HomeIdx = node_id / 1000;
	int ZoneIdx = (node_id % 1000) / 100;
	int ServiceIdx = (node_id % 1000) % 100;

	_log.Log(LOG_TRACE, "Tado: Node " + boost::to_string(node_id) + " = home " + m_TadoHomes[HomeIdx].Name + " zone " + m_TadoHomes[HomeIdx].Zones[ZoneIdx].Name + " device " + boost::to_string(ServiceIdx));

	// ServiceIdx 1 = Away (Read only)
	// ServiceIdx 2 = Setpoint => should be handled in SetSetPoint
	// ServiceIdx 3 = TempHum (Read only)
	// ServiceIdx 4 = Setpoint Override 
	// ServiceIdx 5 = Heating Enabled
	// ServiceIdx 6 = Heating On (Read only)
	// ServiceIdx 7 = Heating Power (Read only)

	// Cancel setpoint override.
	if (ServiceIdx == 4 && !bIsOn) return CancelOverlay(node_id);

	// Enable heating (= cancel overlay that turns off heating)
	if (ServiceIdx == 5 && bIsOn) return CancelOverlay(node_id);

	// Disable heating (= create overlay that turns off heating for an indeterminate amount of time)
	if (ServiceIdx == 5 && !bIsOn) return CreateOverlay(node_id, -1, false, "MANUAL");

	// If the writetohardware command is not handled by now, fail.
	return false;
}

// Changing the setpoint or heating mode is an overlay on the schedule. 
// An overlay can end automatically (TADO_MODE, TIMER) or manually (MANUAL).
bool CTado::CreateOverlay(const int idx, const float temp, const bool heatingEnabled, const std::string terminationType = "TADO_MODE")
{
	_log.Log(LOG_NORM, "Tado: CreateOverlay() called with idx=" + boost::to_string(idx) + ", temp=" + boost::to_string(temp)+", termination type="+terminationType);

	int HomeIdx = idx / 1000;
	int ZoneIdx = (idx % 1000) / 100;
	int ServiceIdx = (idx % 1000) % 100;

	// Check if the home and zone actually exist.
	if (!& m_TadoHomes[HomeIdx] || !& m_TadoHomes[HomeIdx].Zones[ZoneIdx])
	{
		_log.Log(LOG_ERROR, "Tado: no such home/zone combo found: " + boost::to_string(HomeIdx) + "/" + boost::to_string(ZoneIdx));
		return false;
	}

	_log.Log(LOG_TRACE, "Tado: Node " + boost::to_string(idx) + " = home " + m_TadoHomes[HomeIdx].Name + " zone " + m_TadoHomes[HomeIdx].Zones[ZoneIdx].Name + " device " + boost::to_string(ServiceIdx));

	std::string _sUrl = m_TadoEnvironment["tgaRestApiV2Endpoint"] + "/homes/" + m_TadoHomes[HomeIdx].Id + "/zones/" + m_TadoHomes[HomeIdx].Zones[ZoneIdx].Id + "/overlay";
	std::string _sResponse;
	Json::Value _jsPostData;
	Json::Value _jsPostDataSetting;

	Json::Value _jsPostDataTermination;
	_jsPostDataSetting["type"] = "HEATING";
	_jsPostDataSetting["power"] = (heatingEnabled ? "ON" : "OFF");

	if (temp > -1)
	{
		Json::Value _jsPostDataSettingTemperature;
		_jsPostDataSettingTemperature["celsius"] = temp;
		_jsPostDataSetting["temperature"] = _jsPostDataSettingTemperature;
	}

	_jsPostData["setting"] = _jsPostDataSetting;
	_jsPostDataTermination["type"] = terminationType;
	_jsPostData["termination"] = _jsPostDataTermination;

	Json::Value _jsRoot;

	if (!SendToTadoApi(eTadoApiMethod::Put, _sUrl, _jsPostData.toStyledString(), _sResponse, std::vector<std::string> {}, _jsRoot))
	{
		_log.Log(LOG_ERROR, "Tado: Failed to set setpoint via Api.");
		return false;
	}

	_log.Log(LOG_TRACE, "Tado: Response: " + _sResponse);

	/* if (temp > -1)
	{
		if (_jsRoot["setting"]["temperature"]["celsius"].asFloat() == temp) {
			SendSetPointSensor(idx, temp, m_TadoHomes[HomeIdx].Name + " " + m_TadoHomes[HomeIdx].Zones[ZoneIdx].Name + " Setpoint");
		}
	} */

	// Trigger a zone refresh
	GetZoneState(HomeIdx, ZoneIdx, m_TadoHomes[HomeIdx], m_TadoHomes[HomeIdx].Zones[ZoneIdx]);

	return true;
}

void CTado::SetSetpoint(const int idx, const float temp)
{
	_log.Log(LOG_NORM, "Tado: SetSetpoint() called with idx=" + boost::to_string(idx) + ", temp=" + boost::to_string(temp));
	CreateOverlay(idx, temp, true, "TADO_MODE");
}

// Requests an authentication token from the Tado OAuth Api.
bool CTado::GetAuthToken(std::string &authtoken, std::string &refreshtoken, const bool refreshUsingToken = false)
{
	if (m_TadoUsername.size() == 0 && !refreshUsingToken) 
	{
		_log.Log(LOG_ERROR, "Tado: No username specified.");
		return false;
	}

	if (m_TadoPassword.size() == 0 && !refreshUsingToken)
	{
		_log.Log(LOG_ERROR, "Tado: No password specified.");
		return false;
	}

	if (m_bDoGetEnvironment){
		_log.Log(LOG_ERROR, "Tado: Environment not (yet) set up.");
		return false;
	}

	std::string _sUrl = m_TadoEnvironment["apiEndpoint"] + "/token";
	std::ostringstream s;
	std::string _sGrantType = (refreshUsingToken ? "refresh_token" : "password");

	s << "client_id=" << m_TadoEnvironment["clientId"] << "&grant_type=";
	s << _sGrantType << "&scope=home.user&client_secret=";
	s << m_TadoEnvironment["clientSecret"];

	if (refreshUsingToken)
	{
		s << "&refresh_token=" << refreshtoken;
	}
	else
	{
		s << "&password=" << CURLEncode::URLEncode(m_TadoPassword);
		s << "&username=" << CURLEncode::URLEncode(m_TadoUsername);
	}

	std::string sPostData = s.str();

	std::string _sResponse;
	std::vector<std::string> _vExtraHeaders = { "Content-Type: application/x-www-form-urlencoded" };
	std::vector<std::string> _vResponseHeaders;

	Json::Value _jsRoot;

	if (!SendToTadoApi(eTadoApiMethod::Post, _sUrl, sPostData, _sResponse, _vExtraHeaders, _jsRoot, true, false, false))
	{
		_log.Log(LOG_ERROR, "Tado: failed to get token.");
		return false;
	}

	authtoken = _jsRoot["access_token"].asString();
	if (authtoken.size() == 0)
	{
		_log.Log(LOG_ERROR, "Tado: received token is zero length.");
		return false;
	}

	refreshtoken = _jsRoot["refresh_token"].asString();
	if (refreshtoken.size() == 0)
	{
		_log.Log(LOG_ERROR, "Tado: received refresh token is zero length.");
		return false;
	}
	
	_log.Log(LOG_STATUS, "Tado: Received access token from API.");
	_log.Log(LOG_STATUS, "Tado: Received refresh token from API.");

	return true;
}

// Gets the status information of a zone. 
bool CTado::GetZoneState(const int HomeIndex, const int ZoneIndex, const _tTadoHome home, _tTadoZone &zone)
{
	std::string _sUrl = m_TadoEnvironment["tgaRestApiV2Endpoint"] + "/homes/" + zone.HomeId + "/zones/" + zone.Id + "/state";
	Json::Value _jsRoot;
	std::string _sResponse;

	if (!SendToTadoApi(eTadoApiMethod::Get, _sUrl, "", _sResponse, std::vector<std::string> {}, _jsRoot))
	{
		_log.Log(LOG_ERROR, "Tado: failed to get information on zone " + zone.Name);
		return false;
	}

	// Zone Home/away
	bool _bTadoAway = !(_jsRoot["tadoMode"].asString() == "HOME");
	UpdateSwitch((unsigned char)ZoneIndex * 100 + 1, _bTadoAway, home.Name + " " + zone.Name + " Away");

	// Zone setpoint
	float _fSetpointC = 0;
	if (_jsRoot["setting"]["temperature"]["celsius"].isNumeric())
		_fSetpointC = _jsRoot["setting"]["temperature"]["celsius"].asFloat();
	if (_fSetpointC > 0) {
		SendSetPointSensor((unsigned char)ZoneIndex * 100 + 2, _fSetpointC, home.Name + " " + zone.Name + " Setpoint");
	}

	// Current zone inside temperature
	float _fCurrentTempC = 0;
	if (_jsRoot["sensorDataPoints"]["insideTemperature"]["celsius"].isNumeric())
		_fCurrentTempC = _jsRoot["sensorDataPoints"]["insideTemperature"]["celsius"].asFloat();

	// Current zone humidity
	float fCurrentHumPct = 0;
	if (_jsRoot["sensorDataPoints"]["humidity"]["percentage"].isNumeric())
		fCurrentHumPct = _jsRoot["sensorDataPoints"]["humidity"]["percentage"].asFloat();
	if (_fCurrentTempC > 0) {
		SendTempHumSensor(ZoneIndex * 100 + 3, 255, _fCurrentTempC, (int)fCurrentHumPct, home.Name + " " + zone.Name + " TempHum");
	}

	// Manual override of zone setpoint
	bool _bManualControl = false;
	if (!_jsRoot["overlay"].isNull() && _jsRoot["overlay"]["type"].asString() == "MANUAL")
	{
		_bManualControl = true;
	}
	UpdateSwitch((unsigned char)ZoneIndex * 100 + 4, _bManualControl, home.Name + " " + zone.Name + " Manual Setpoint Override");


	// Heating Enabled
	std::string _sType = _jsRoot["setting"]["type"].asString();
	std::string _sPower = _jsRoot["setting"]["power"].asString();
	bool _bHeatingEnabled = false;
	if (_sType == "HEATING" && _sPower == "ON")
		_bHeatingEnabled = true;
	UpdateSwitch((unsigned char)ZoneIndex * 100 + 5, _bHeatingEnabled, home.Name + " " + zone.Name + " Heating Enabled");

	// Heating Power percentage
	std::string _sHeatingPowerType = _jsRoot["activityDataPoints"]["heatingPower"]["type"].asString();
	int _sHeatingPowerPercentage = _jsRoot["activityDataPoints"]["heatingPower"]["percentage"].asInt();
	bool _bHeatingOn = false;
	if (_sHeatingPowerType == "PERCENTAGE" && _sHeatingPowerPercentage >= 0 && _sHeatingPowerPercentage <= 100)
	{
		_bHeatingOn = _sHeatingPowerPercentage > 0;
		UpdateSwitch((unsigned char)ZoneIndex * 100 + 6, _bHeatingOn, home.Name + " " + zone.Name + " Heating On");
		
		SendPercentageSensor(ZoneIndex * 100 + 7, 0, 255, (float)_sHeatingPowerPercentage, home.Name + " " + zone.Name + " Heating Power");
	}

	return true;
}

void CTado::SendSetPointSensor(const unsigned char Idx, const float Temp, const std::string & defaultname)
{
	_tThermostat thermos;
	thermos.subtype = sTypeThermSetpoint;
	thermos.id1 = 0;
	thermos.id2 = 0;
	thermos.id3 = 0;
	thermos.id4 = Idx;
	thermos.dunit = 0;

	thermos.temp = Temp;

	sDecodeRXMessage(this, (const unsigned char *)&thermos, defaultname.c_str(), 255);
}

// Creates or updates on/off switches.
void CTado::UpdateSwitch(const unsigned char Idx, const bool bOn, const std::string &defaultname)
{
	bool bDeviceExits = true;
	char szIdx[10];
	sprintf(szIdx, "%X%02X%02X%02X", 0, 0, 0, Idx);
	std::vector<std::vector<std::string> > result;
	result = m_sql.safe_query("SELECT Name,nValue,sValue FROM DeviceStatus WHERE (HardwareID==%d) AND (Type==%d) AND (SubType==%d) AND (DeviceID=='%q')",
		m_HwdID, pTypeLighting2, sTypeAC, szIdx);
	if (!result.empty())
	{
		//check if we have a change, if not do not update it
		int nvalue = atoi(result[0][1].c_str());
		if ((!bOn) && (nvalue == 0))
			return;
		if ((bOn && (nvalue != 0)))
			return;
	}

	//Send as Lighting 2
	tRBUF lcmd;
	memset(&lcmd, 0, sizeof(RBUF));
	lcmd.LIGHTING2.packetlength = sizeof(lcmd.LIGHTING2) - 1;
	lcmd.LIGHTING2.packettype = pTypeLighting2;
	lcmd.LIGHTING2.subtype = sTypeAC;
	lcmd.LIGHTING2.id1 = 0;
	lcmd.LIGHTING2.id2 = 0;
	lcmd.LIGHTING2.id3 = 0;
	lcmd.LIGHTING2.id4 = Idx;
	lcmd.LIGHTING2.unitcode = 1;
	int level = 15;
	if (!bOn)
	{
		level = 0;
		lcmd.LIGHTING2.cmnd = light2_sOff;
	}
	else
	{
		level = 15;
		lcmd.LIGHTING2.cmnd = light2_sOn;
	}
	lcmd.LIGHTING2.level = level;
	lcmd.LIGHTING2.filler = 0;
	lcmd.LIGHTING2.rssi = 12;
	sDecodeRXMessage(this, (const unsigned char *)&lcmd.LIGHTING2, defaultname.c_str(), 255);
}

// Removes any active overlay from a specific zone.
bool CTado::CancelOverlay(const int Idx)
{
	_log.Log(LOG_TRACE, "Tado: CancelSetpointOverlay() called with idx=" + boost::to_string(Idx));

	int HomeIdx = Idx / 1000;
	int ZoneIdx = (Idx % 1000) / 100;
	int ServiceIdx = (Idx % 1000) % 100;

	// Check if the home and zone actually exist.
	if (!& m_TadoHomes[HomeIdx] || !& m_TadoHomes[HomeIdx].Zones[ZoneIdx])
	{
		_log.Log(LOG_ERROR, "Tado: no such home/zone combo found: " + boost::to_string(HomeIdx) + "/" + boost::to_string(ZoneIdx));
		return false;
	}

	std::string _sUrl = m_TadoEnvironment["tgaRestApiV2Endpoint"] + "/homes/" + m_TadoHomes[HomeIdx].Id + "/zones/" + m_TadoHomes[HomeIdx].Zones[ZoneIdx].Id + "/overlay";
	std::string _sResponse;

	if (!SendToTadoApi(eTadoApiMethod::Delete, _sUrl, "", _sResponse, std::vector<std::string>{}, *(new Json::Value), false, true, true))
	{
		_log.Log(LOG_ERROR, "Tado: error cancelling the setpoint overlay.");
		return false;
	}
	_log.Log(LOG_STATUS, "Tado: Setpoint overlay cancelled.");

	// Trigger a zone refresh
	GetZoneState(HomeIdx, ZoneIdx, m_TadoHomes[HomeIdx], m_TadoHomes[HomeIdx].Zones[ZoneIdx]);

	return true;
}

void CTado::Do_Work()
{
	_log.Log(LOG_TRACE, "Tado: Do_Work() called.");
	_log.Log(LOG_STATUS, "Tado: Worker started. Will poll every " + boost::to_string(TADO_POLL_INTERVAL) + " seconds.");
	int iSecCounter = TADO_POLL_INTERVAL - 5;
	int iTokenCycleCount = 0;

	while (!m_stoprequested)
	{
		sleep_seconds(1);
		iSecCounter++;
		if (iSecCounter % 12 == 0)
		{
			m_LastHeartbeat = mytime(NULL);
		}

		if (iSecCounter % TADO_POLL_INTERVAL == 0)
		{
			// Only login if we should.
			if (m_bDoLogin)
			{
				// Lets try to get authentication set up. If not, stop the worker.
				if (!Login()) m_stoprequested = true;
			}

			// Check if we should get homes from tado account
			if (m_bDoGetHomes)
			{
				// If we fail to do so, abort.
				if (!GetHomes())
				{
					_log.Log(LOG_ERROR, "Tado: Failed to get homes from Tado account.");
					m_stoprequested = true;
				}
				// Else move on to getting zones for each of the homes.
				else m_bDoGetZones = true;
			}

			// Check if we should be collecting zones for each of the homes.
			if (m_bDoGetZones)
			{
				for (int i = 0; i < (int)m_TadoHomes.size(); i++)
				{
					GetZones(m_TadoHomes[i]);
				}
			}

			// Check if the authentication token is still useable.
			if (iTokenCycleCount++ > TADO_TOKEN_MAXLOOPS)
			{
				_log.Log(LOG_NORM, "Tado: refreshing token.");
				if (!GetAuthToken(m_TadoAuthToken, m_TadoRefreshToken, true)) {
					_log.Log(LOG_ERROR, "Tado: failed to refresh authentication token. Skipping this cycle.");
				}
				// Reset the counter back to the maximum
				else iTokenCycleCount = TADO_TOKEN_MAXLOOPS;
			}
			// Increase the number of cycles that the token has been used by 1.
			else iTokenCycleCount++;

			// Iterate through the discovered homes and zones. Get some state information.
			for (int HomeIndex = 0; HomeIndex < (int)m_TadoHomes.size(); HomeIndex++) {
				for (int ZoneIndex = 0; ZoneIndex < (int)m_TadoHomes[HomeIndex].Zones.size(); ZoneIndex++)
				{
					if (!GetZoneState(HomeIndex, ZoneIndex, m_TadoHomes[HomeIndex], m_TadoHomes[HomeIndex].Zones[ZoneIndex])) 
					{
						_log.Log(LOG_ERROR, "Tado: Failed to get state for home '" + m_TadoHomes[HomeIndex].Name + "', zone '" + m_TadoHomes[HomeIndex].Zones[ZoneIndex].Name + "'");
					}
				}
			}
		}
	}
	_log.Log(LOG_STATUS, "Tado: Worker stopped.");
}

// Goes through the Tado web interface environment file and attempts to regex match the specified key.
bool CTado::MatchValueFromJSKey(const std::string sKeyName, const std::string sJavascriptData, std::string &sValue)
{
	std::match_results<std::string::const_iterator> _Matches;

	// Grab the "clientId" from the response. 
	std::regex _reSearch(sKeyName + ": '(.*?)'");
	if (!std::regex_search(sJavascriptData, _Matches, _reSearch)) {
		_log.Log(LOG_ERROR, "Tado: Failed to grab "+sKeyName+" from the javascript data.");
		return false;
	}
	sValue = _Matches[1];
	if (sValue.size() == 0)
	{
		_log.Log(LOG_ERROR, "Tado: Value for key "+sKeyName+" is zero length.");
		return false;
	}
	return true;
}


// Grabs the web app environment file
bool CTado::GetTadoApiEnvironment(std::string sUrl)
{
	_log.Log(LOG_TRACE, "Tado: GetTadoApiEnvironment called with sUrl="+sUrl);
	// This is a bit of a special case. Since we pretend to be the web
	// application (my.tado.com) we have to play by its rules. It works
	// with some information like a client id and a client secret. We 
	// have to pluck that environment information from the web page and 
	// then parse it so we can use it in our future calls.

	std::string _sResponse;
	std::match_results<std::string::const_iterator> _Matches;

	// Download the API environment file
	if (!HTTPClient::GET(sUrl, _sResponse, false)) {
		_log.Log(LOG_ERROR, "Tado: Failed to retrieve API environment from "+sUrl);
		return false;
	}

	// Determine which keys we want to grab from the environment
	std::vector<std::string> vKeysToFetch = { "clientId", "clientSecret", "apiEndpoint", "tgaRestApiV2Endpoint" };

	// The keys will be stored in a map, lets clean it out first.
	m_TadoEnvironment.clear();

	for (int i = 0; i < (int)vKeysToFetch.size(); i++)
	{
		// Feed the function the javascript response, and have it attempt to grab the provided key's value from it.
		// Value is stored in m_TadoEnvironment[keyName]
		std::string _sKeyName = vKeysToFetch[i];
		if (!MatchValueFromJSKey(_sKeyName, _sResponse, m_TadoEnvironment[_sKeyName])) {
			_log.Log(LOG_ERROR, "Tado: Failed to retrieve/match key '" + _sKeyName + "' from the API environment.");
			return false;
		}
	}

	_log.Log(LOG_STATUS, "Tado: Retrieved webapp environment from API.");
	
	// Mark this action as completed.
	m_bDoGetEnvironment = false;
	return true;
}

// Sets up the environment and grabs an auth token.
bool CTado::Login()
{
	_log.Log(LOG_TRACE, "Tado: Login() called.");
	_log.Log(LOG_NORM, "Tado: Attempting login.");

	if (m_bDoGetEnvironment) {
		// First get information about the environment of the web application.
		if (!GetTadoApiEnvironment(TADO_API_ENVIRONMENT_URL))
		{
			return false;
		}
	}

	// Now fetch the token.
	if (!GetAuthToken(m_TadoAuthToken, m_TadoRefreshToken, false)) 
	{
		return false;
	}

	_log.Log(LOG_NORM, "Tado: Login succesful.");

	// We have logged in, no need to do it again.
	m_bDoLogin = false;

	return true;
}

// Gets all the homes in the account.
bool CTado::GetHomes() {
	_log.Log(LOG_TRACE, "Tado: GetHomes() called.");

	std::string _sUrl = m_TadoEnvironment["tgaRestApiV2Endpoint"] + "/me";
	Json::Value _jsRoot;
	std::string _sResponse;
	
	if (!SendToTadoApi(eTadoApiMethod::Get, _sUrl, "", _sResponse, std::vector<std::string>{}, _jsRoot))
	{
		_log.Log(LOG_ERROR, "Tado: failed to get homes.");
		return false;
	}

	// Make sure we start with an empty list.
	m_TadoHomes.clear();

	Json::Value _jsAllHomes = _jsRoot["homes"];
	_log.Log(LOG_NORM, "Tado: Found homes: " + boost::to_string(_jsAllHomes.size()));

	for (int i = 0; i < (int)_jsAllHomes.size(); i++) {
		// Store the tado home information in a map.

		if (!_jsAllHomes[i].isObject()) continue;

		_tTadoHome _structTadoHome;
		_structTadoHome.Name = _jsAllHomes[i]["name"].asString();
		_structTadoHome.Id = _jsAllHomes[i]["id"].asString();
		m_TadoHomes.push_back(_structTadoHome);

		_log.Log(LOG_STATUS, "Tado: Registered home '" + _structTadoHome.Name + "' with id " + _structTadoHome.Id);
	}
	// Sort the homes so they end up in the same order every time.
	std::sort(m_TadoHomes.begin(), m_TadoHomes.end());

	return true;
}

// Gets all the zones for a particular home
bool CTado::GetZones(_tTadoHome &tTadoHome) {

	std::string _sUrl = m_TadoEnvironment["tgaRestApiV2Endpoint"] + "/homes/" + tTadoHome.Id + "/zones";
	std::string _sResponse;
	Json::Value _jsRoot;

	tTadoHome.Zones.clear();

	if (!SendToTadoApi(eTadoApiMethod::Get, _sUrl, "", _sResponse, std::vector<std::string>{}, _jsRoot))
	{
		_log.Log(LOG_ERROR, "Tado: Failed to get zones from API for home " + tTadoHome.Id);
		return false;
	}

	// Loop through each of the zones
	for (int zoneIdx = 0; zoneIdx < (int)_jsRoot.size(); zoneIdx++) {
		_tTadoZone _TadoZone;

		_TadoZone.HomeId = tTadoHome.Id;
		_TadoZone.Id = _jsRoot[zoneIdx]["id"].asString();
		_TadoZone.Name = _jsRoot[zoneIdx]["name"].asString();
		_TadoZone.Type = _jsRoot[zoneIdx]["type"].asString();

		_log.Log(LOG_NORM, "Tado: Registered Zone " + _TadoZone.Id + " '" + _TadoZone.Name + "' of type " + _TadoZone.Type);

		tTadoHome.Zones.push_back(_TadoZone);
	}
	
	// Sort the zones based on Id (defined in structure) so we always get them in the same order.
	std::sort(tTadoHome.Zones.begin(), tTadoHome.Zones.end());

	for (int i = 0; (unsigned int)i < tTadoHome.Zones.size(); i++) {
		_log.Log(LOG_NORM, "Zone: " + tTadoHome.Zones[i].Name);
	}

	return true;
}

// Sends a request to the Tado API. 
bool CTado::SendToTadoApi(const eTadoApiMethod eMethod, const std::string sUrl, const std::string sPostData, 
				std::string &sResponse, const std::vector<std::string> & vExtraHeaders, Json::Value &jsDecodedResponse, 
				const bool bDecodeJsonResponse, const bool bIgnoreEmptyResponse, const bool bSendAuthHeaders)
{
	try {
		// If there is no token stored then there is no point in doing a request. Unless we specifically
		// decide not to do authentication.
		if (m_TadoAuthToken.size() == 0 && bSendAuthHeaders) throw std::runtime_error("No access token available.");

		// Prepare the headers. Copy supplied vector.
		std::vector<std::string> _vExtraHeaders = vExtraHeaders;
		
		if (sPostData.size() > 0)
		{
			Json::Reader _jsReader;
			if (_jsReader.parse(sPostData, *(new Json::Value))) {
				_vExtraHeaders.push_back("Content-Type: application/json");
			}
		}
		
		// Prepare the authentication headers if requested.
		if (bSendAuthHeaders) _vExtraHeaders.push_back("Authorization: Bearer " + m_TadoAuthToken);

		switch (eMethod)
		{
			case Put:
				if (!HTTPClient::PUT(sUrl, sPostData, _vExtraHeaders, sResponse, bIgnoreEmptyResponse))
				{
					throw std::runtime_error("Failed to perform PUT request to Tado Api: " + sResponse);
				}
				break;

			case Post:
				if (!HTTPClient::POST(sUrl, sPostData, _vExtraHeaders, sResponse, true, bIgnoreEmptyResponse))
				{
					throw std::runtime_error("Failed to perform POST request to Tado Api: " + sResponse);
				}
				break;

			case Get:
				if (!HTTPClient::GET(sUrl, _vExtraHeaders, sResponse, bIgnoreEmptyResponse))
				{
					throw std::runtime_error("Failed to perform GET request to Tado Api: " + sResponse);
				}
				break;

			case Delete:
				if (!HTTPClient::Delete(sUrl, sPostData, _vExtraHeaders, sResponse, bIgnoreEmptyResponse)) {
					{
						throw std::runtime_error("Failed to perform DELETE request to Tado Api: "+sResponse);
					}
				}
				break;

			default:
				throw std::runtime_error("Unknown method " + boost::to_string(eMethod));
		}

		if (sResponse.size() == 0)
		{
			if (!bIgnoreEmptyResponse) throw std::runtime_error("Received an empty response from Api.");
		}

		if (bDecodeJsonResponse) {
			Json::Reader _jsReader;
			if (!_jsReader.parse(sResponse, jsDecodedResponse)) {
				throw std::runtime_error("Failed to decode Json response from Api.");
			}
		}

		return true;
	}
	catch (std::exception e)
	{
		std::string _sWhat = e.what();
		_log.Log(LOG_ERROR, "Tado: error sending information to Tado API: "+_sWhat);
		return false;
	}
}
