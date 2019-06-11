/*
File : MultiblocV8.cpp
Author : X.PONCET
Version : 1.00
Description : This class manage the CAN MultiblocV8 Layer
- Management of the receiveing Frame
- Treatment of each Life frame receive
- Up to 30 blocs can be manage actually !
- Complete management of input/output of SFSP Blocs (Master and Slave) up to 8 Master and 8 slaves in one CAN network
- On one SFSP blocs : 6 PWM output, 1 Power Voltage reading, 4 wired input and for Master blocs : each wireless switch receive is created in the domoticz dbb.
- Description of the SFSP bloc : http://www.scheiber.com/doc_technique/sfsp-2012-a1/

History :
- 2017-10-01 : Creation by X.PONCET

- 2018-01-22 : Update :
# add feature : manual creation up to 127 virtual switch, ability to learn eatch switch to any blocks output
# add feature : now possibility to Enter/Exit Learn mode Or Clear Mode for all SFSP Blocks 
	(each blocks detected blocks automatically creates 3 associated buttons for Learn/Exit Learn and Clear, usefull if the blocks is not accessible )

	
*/
#include "stdafx.h"
#include "USBtin_MultiblocV8.h"
#include "hardwaretypes.h"
#include "../main/Logger.h"
#include "../main/localtime_r.h"
#include "../main/RFXtrx.h"
#include "../main/Helper.h"
#include "../main/SQLHelper.h"

#include <iomanip>
#include <locale>
#include <sstream>
#include <string>

#include <bitset>			 // This is necessary to compile on Windows

#define	TIME_1sec				1000
#define	TIME_500ms				500
#define	TIME_200ms				200
#define	TIME_100ms				100
#define	TIME_50ms				50
#define	TIME_10ms				10
#define	TIME_5ms				5

#define BLOC_ALIVE				1
#define BLOC_NOTALIVE			2

#define MSK_TYPE_TRAME          0x1FFE0000
#define MSK_INDEX_MODULE        0x0001FF80
#define MSK_CODAGE_MODULE       0x00000078
#define MSK_SRES_MODULE         0x00000007

#define SHIFT_TYPE_TRAME        17 
#define SHIFT_INDEX_MODULE      7
#define SHIFT_CODAGE_MODULE     3
#define SHIFT_SRES_MODULE       0

#define type_ALIVE_FRAME            0
#define type_LIFE_PHASE_FRAME       1
#define type_TP_DATA                2
#define type_TP_FLOW_CONTROL        3
#define type_DATE_TIME              4
#define type_ETAT_BLOC              5
#define type_COMMANDE_ETAT_BLOC     6

#define type_CTRL_FRAME_BLOC        256
#define type_CTRL_IO_BLOC           257

#define type_E_ANA                  258
#define type_E_ANA_1_TO_4           258
#define type_E_ANA_5_TO_8           259
#define type_E_ANA_9_TO_12          260
#define type_E_ANA_13_TO_16         261
#define type_E_ANA_17_TO_20         262
#define type_E_ANA_21_TO_24         263
#define type_E_ANA_25_TO_28         264
#define type_E_ANA_29_TO_32         265

#define type_E_TOR                  266
#define type_E_TOR_1_TO_64          266

#define type_S_TOR                  267
#define type_STATE_S_TOR_1_TO_2     267
#define type_STATE_S_TOR_3_TO_4     268
#define type_STATE_S_TOR_5_TO_6     269
#define type_STATE_S_TOR_7_TO_8     270
#define type_STATE_S_TOR_9_TO_10    271
#define type_STATE_S_TOR_11_TO_12   272
#define type_STATE_S_TOR_13_TO_14   273
#define type_STATE_S_TOR_15_TO_16   274
#define type_STATE_S_TOR_17_TO_18   275
#define type_STATE_S_TOR_19_TO_20   276
#define type_STATE_S_TOR_21_TO_22   277
#define type_STATE_S_TOR_23_TO_24   278
#define type_STATE_S_TOR_25_TO_26   279
#define type_STATE_S_TOR_27_TO_28   280
#define type_STATE_S_TOR_29_TO_30   281
#define type_STATE_S_TOR_31_TO_32   282

#define type_CMD_S_TOR              283
#define type_CONSO_S_TOR_1_TO_8     284
#define type_CONSO_S_TOR_9_TO_16    285
#define type_CONSO_S_TOR_17_TO_24   286
#define type_CONSO_S_TOR_25_TO_32   287
#define type_CONSO_S_TOR            type_CONSO_S_TOR_1_TO_8

#define type_SFSP_SWITCH            512
#define type_SFSP_LED_CMD           513
#define type_SFSP_CAPTEUR_1BS       514
#define type_SFSP_CAPTEUR_4BS       515
#define type_SFSP_SYNCHRO           516
#define type_SFSP_LearnCommand      517

#define BLOC_NORMAL_STATES			0x00
#define BLOC_STATES_OFF				0x01
#define BLOC_STATES_RESET			0x02
#define BLOC_STATES_LEARNING		0x03
#define BLOC_STATES_LEARNING_STOP	0x04
#define	BLOC_STATES_CLEARING		0x05

//xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
// REFERENCE INDEX
//xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
#define BLOC_DOMOTICZ                   0x01
#define BLOC_SFSP_M                     0x14
#define BLOC_SFSP_E                     0x15

std::string NomRefBloc[45]={
	"UNDEFINED",
	"DOMOTICZ",
	"NAVIGRAPH_VER",
	"NAVIGRAPH_HOR",
	"NAVIGRAPH_HOR_RTC",
	"NAVICOLOR",
	"NAVICOLOR_RTC",
	"BLOC_1",
	"BLOC_2",
	"BLOC_4",
	"BLOC_6",
	"BLOC_7",
	"BLOC_8",
	"BLOC_9",
	"BLOC_AU",
	"BLOC_CC",
	"BLOC_TF",
	"BLOC_AC1",
	"BLOC_X98",
	"BLOC_RESPIRE",
	"BLOC_SFSP_M",
	"BLOC_SFSP_E",
	"BLOC_SFSP_A",
	"SELECTEUR_AC1",
	"CHARGEUR_MDP",
	"MULTICOM",
	"MULTICOM_V2",
	"INTERFACE_CAN_CAN",
	"INTERFACE_CAN_CAN_V2",
	"INTERFACE_BLUETOOTH",
	"INTERFACE_LIN",
	"NAVICOLOR_GT",
	"CHARGEUR_CRISTEC",
	"BLOC_VENTIL",
	"BLOC_SIGNAL",
	"BLOC_PROTECT",
	"BLOC_X10",
	"AMPLI_ATOLL",
	"AMPLI_ATOLL_3ZONES",
	"SERVEUR_WIFI",
	"NAVICOLOR_PT",
	"NAVICOLOR_PT2",
	"CLIM_DOMETIC",
	"FACADE_CARLING",
	" ",
	// "SELECTEUR_MDP",
};

USBtin_MultiblocV8::USBtin_MultiblocV8()
{
	m_stoprequested = false;
	BOOL_DebugInMultiblocV8 = false;
}

USBtin_MultiblocV8::~USBtin_MultiblocV8(){
	StopThread();
}

bool USBtin_MultiblocV8::StartThread()
{
	m_stoprequested = false;
	//Id Base for manual creation switch from domoticz...
	V8switch_id_base = (type_SFSP_SWITCH<<SHIFT_TYPE_TRAME)+(1<<SHIFT_INDEX_MODULE)+(0<<SHIFT_CODAGE_MODULE)+0;
	min_counter = (60*5);
	min_counter2 = (3600*6);
	m_thread = boost::shared_ptr<boost::thread>(new boost::thread(boost::bind(&USBtin_MultiblocV8::Do_Work, this)));
	_log.Log(LOG_STATUS,"MultiblocV8: thread started");
	return (m_thread != NULL);
}

void USBtin_MultiblocV8::StopThread()
{
	try {
		if (m_thread)
		{
			m_stoprequested = true;
			m_thread->join();
			sleep_milliseconds(20); //wait time
			_log.Log(LOG_STATUS,"MultiblocV8: thread stopped");
		}
		ClearingBlocList();
	}
	catch (...)
	{
		_log.Log(LOG_STATUS,"MultiblocV8: thread not stopped");
		//Don't throw from a Stop command
	}
}

void USBtin_MultiblocV8::ManageThreadV8(bool States){
	if( States == true) StartThread();
	else StopThread();
}

void USBtin_MultiblocV8::ClearingBlocList(){
	_log.Log(LOG_NORM,"MultiblocV8: clearing BlocList");
	//effacement du tableau � l'init
	for(int i = 0;i < MAX_NUMBER_BLOC;i++){
		BlocList_CAN[i].BlocID = 0;
		BlocList_CAN[i].Status = 0;
		BlocList_CAN[i].NbAliveFrameReceived = 0;
		BlocList_CAN[i].VersionH = 0;
		BlocList_CAN[i].VersionM = 0;
		BlocList_CAN[i].VersionL = 0;
		BlocList_CAN[i].CongifurationCrc = 0;
	}
}

void USBtin_MultiblocV8::Do_Work()
{
	ClearingBlocList();
	//call every 100ms.... so...
	while( !m_stoprequested ){
		sleep_milliseconds(TIME_100ms);
		
		sec_counter++;
		if (sec_counter >= 10)
		{
			sec_counter = 0;
			min_counter--;
			min_counter2--;
			
			if( min_counter == 0 ){
				//each 5 minutes
				min_counter = (60*5);
				BOOL_TaskAGo = true;
			}
			
			if( min_counter2 == 0 ){
				//each 6 hours...
				min_counter2 = (3600*6);
				BOOL_TaskRqStorGo = true;
			}
			
			Asec_counter++;
			if( Asec_counter >= 3 ){
				Asec_counter = 0;
				//each 3 seconds
				BlocList_CheckBloc();
			}
			
		}
		
		if( BOOL_SendPushOffSwitch ){ //if a Send push off switch is request
			BOOL_SendPushOffSwitch = false;
			USBtin_MultiblocV8_Send_SFSPSwitch_OnCAN(Sid_PushOff_ToSend,CodeTouchePushOff_ToSend);
		}
		
	}
}

void USBtin_MultiblocV8::SendRequest(int sID ){
	char szDeviceID[10];
	std::string szTrameToSend = "R"; // "R02180A008"; //for debug to force request
	sprintf(szDeviceID,"%08X",(unsigned int)sID);
	szTrameToSend += szDeviceID;
	szTrameToSend += "8";
	//_log.Log(LOG_NORM,"MultiblocV8: write frame to Can: #%s# ",szTrameToSend.c_str());
	writeFrame(szTrameToSend);
	sleep_milliseconds(TIME_5ms); //wait time to not overrun can bus if many request are send...
}

void USBtin_MultiblocV8::Traitement_MultiblocV8(int IDhexNumber,unsigned int rxDLC, unsigned int Buffer_Octets[8]){
	// d�composition de l'identifiant qui contient les informations suivante :
	// identifier parsing which contains the following information :
	int FrameType = (IDhexNumber & MSK_TYPE_TRAME) >> SHIFT_TYPE_TRAME;
	char RefBloc = (IDhexNumber & MSK_INDEX_MODULE) >> SHIFT_INDEX_MODULE;
	char Codage = (IDhexNumber & MSK_CODAGE_MODULE) >> SHIFT_CODAGE_MODULE;
	char SsReseau = IDhexNumber & MSK_SRES_MODULE;

	if( m_thread ){
		switch(FrameType){ //First switch !
			case type_ALIVE_FRAME:
				// cr�ation/update d'un composant d�tecter grace � sa trame de vie :
				// creates/updates of a blocs detected with his life frame :
				//_log.Log(LOG_NORM,"MultiblocV8: Life Frame, ref: %#x Codage : %d Network: %d",RefBloc,Codage,SsReseau);
				Traitement_Trame_Vie(RefBloc,Codage,SsReseau,rxDLC,Buffer_Octets);
				break;
				
			case type_STATE_S_TOR_1_TO_2 :
			case type_STATE_S_TOR_3_TO_4 :
			case type_STATE_S_TOR_5_TO_6 :
			case type_STATE_S_TOR_7_TO_8 :
			case type_STATE_S_TOR_9_TO_10:
				//_log.Log(LOG_NORM,"MultiblocV8: States Frame S_TOR, ref: %#x Codage : %d S/R�seau: %d",RefBloc,Codage,SsReseau);
				Traitement_Etat_S_TOR_Recu(FrameType,RefBloc,Codage,SsReseau,Buffer_Octets);
				break;
				
			case type_E_ANA_1_TO_4 :
			case type_E_ANA_5_TO_8 :
				Traitement_E_ANA_Recu(FrameType,RefBloc,Codage,SsReseau,Buffer_Octets);
				break;
				
			case type_SFSP_SWITCH :
				if( rxDLC == 5 ) Traitement_SFSP_Switch_Recu(FrameType,RefBloc,Codage,SsReseau,Buffer_Octets);
				break;
			
		}
	}
}

//Traitement trame contenant une info d'un interrupteur SFSP (filaire ou sans fils, EnOcean, etc...)
//Treatment of Frame containing switch information from wireles or hardware input of a blocs :
void USBtin_MultiblocV8::Traitement_SFSP_Switch_Recu(const unsigned int FrameType,const unsigned char RefBloc, const char Codage, const char Ssreseau, unsigned int bufferdata[8])
{
	unsigned long sID=(FrameType<<SHIFT_TYPE_TRAME)+(RefBloc<<SHIFT_INDEX_MODULE)+(Codage<<SHIFT_CODAGE_MODULE)+Ssreseau;
	int i = 0;
	
	//_log.Log(LOG_NORM,"MultiblocV8: receive SFSP Switch: ID: %x Data: %x %x %x %x %x",sID, bufferdata[0], bufferdata[1],bufferdata[2], bufferdata[3], bufferdata[4]);
	unsigned long SwitchId = (bufferdata[0]<<24)+(bufferdata[1]<<16)+(bufferdata[2]<<8)+bufferdata[3];
	std::string defaultname=" ";
	
	_log.Log(LOG_NORM,"MultiblocV8: Receiving SFSP Switch Frame: Id: %s Codage: %d Ssreseau: %d SwitchID: %08X CodeTouche: %02X",NomRefBloc[RefBloc].c_str(),Codage,Ssreseau,SwitchId, bufferdata[4] );
	
	tRBUF lcmd;
	memset(&lcmd, 0, sizeof(RBUF));
	lcmd.LIGHTING2.packetlength = sizeof(lcmd.LIGHTING2) - 1;
	lcmd.LIGHTING2.packettype = pTypeLighting2;
	lcmd.LIGHTING2.subtype = sTypeAC;
	
	if( SwitchId == 0x0001 ){ //hardware input of a blocs :
		lcmd.LIGHTING2.id1 = (sID>>24)&0xff;
		lcmd.LIGHTING2.id2 = (sID>>16)&0xff;
		lcmd.LIGHTING2.id3 = (sID>>8)&0xff;
		lcmd.LIGHTING2.id4 = sID&0xff;
		defaultname = "Bloc input ";
	}
	else{ //it's a wireless switch (like EnOcean) :
		lcmd.LIGHTING2.id1 = bufferdata[0]&0xff;
		lcmd.LIGHTING2.id2 = bufferdata[1]&0xff;
		lcmd.LIGHTING2.id3 = bufferdata[2]&0xff;
		lcmd.LIGHTING2.id4 = bufferdata[3]&0xff;
		defaultname = "Wireless switch";
	}
	
	int CodeNumber = bufferdata[4]&0x7F;
	lcmd.LIGHTING2.unitcode = CodeNumber;
	if( bufferdata[4]&0x80 ) lcmd.LIGHTING2.cmnd = light2_sOn;
	else lcmd.LIGHTING2.cmnd = light2_sOff;
	lcmd.LIGHTING2.level = 0;		
	lcmd.LIGHTING2.filler = 2;
	lcmd.LIGHTING2.rssi = 12;
	//default name creation :
	
	std::ostringstream convert;   // stream used for the conversion
	convert << CodeNumber;
	defaultname += convert.str();
	
	sDecodeRXMessage(this, (const unsigned char *)&lcmd.LIGHTING2, defaultname.c_str(), 255);
	
}

//For listing of detected blocs :
void  USBtin_MultiblocV8::BlocList_GetInfo(const unsigned char RefBloc, const char Codage, const char Ssreseau,unsigned int bufferdata[8])
{
	unsigned long sID=(RefBloc<<SHIFT_INDEX_MODULE)+(Codage<<SHIFT_CODAGE_MODULE)+Ssreseau;
	int i = 0;
	bool BIT_FIND_BLOC = false;
	int IndexBLoc = 0;
	unsigned long Rqid = 0;
	
	for(i = 0;i < MAX_NUMBER_BLOC;i++){
		if( BlocList_CAN[i].BlocID == sID ){
			BIT_FIND_BLOC = true;
			IndexBLoc = i;
			break;
		}
	}
	//if the blocs allready exist :
	if( BIT_FIND_BLOC == true ){
		//on le met � jours :
		BlocList_CAN[IndexBLoc].VersionH = bufferdata[0];
		BlocList_CAN[IndexBLoc].VersionM = bufferdata[1];
		BlocList_CAN[IndexBLoc].VersionL = bufferdata[2];
		BlocList_CAN[IndexBLoc].CongifurationCrc = ( bufferdata[3]<<8 )+bufferdata[4];
		BlocList_CAN[IndexBLoc].Status = BLOC_ALIVE;
		BlocList_CAN[IndexBLoc].NbAliveFrameReceived++;
	}
	else{
		i = 0;
		//or just been detected :
		for(i = 0;i < MAX_NUMBER_BLOC;i++){
			if( BlocList_CAN[i].BlocID == 0 ){
				BlocList_CAN[i].BlocID = sID;
				BlocList_CAN[IndexBLoc].VersionH = bufferdata[0];
				BlocList_CAN[IndexBLoc].VersionM = bufferdata[1];
				BlocList_CAN[IndexBLoc].VersionL = bufferdata[2];
				BlocList_CAN[IndexBLoc].CongifurationCrc = ( bufferdata[3]<<8 )+bufferdata[4];
				BlocList_CAN[IndexBLoc].Status = BLOC_ALIVE;
				BlocList_CAN[IndexBLoc].NbAliveFrameReceived = 0;
				_log.Log(LOG_NORM,"MultiblocV8: new bloc detected: %s# Coding: %d network: %d", NomRefBloc[RefBloc].c_str(),Codage,Ssreseau);
				//checking if we must send request, to refresh the hardware in domoticz dispositifs :
				switch(RefBloc){
					case BLOC_SFSP_M :
					case BLOC_SFSP_E :
						//requete analogique (tension alim) / analog send request for Power tension of SFSP Blocs
						Rqid= (type_E_ANA_1_TO_4<<SHIFT_TYPE_TRAME)+ BlocList_CAN[i].BlocID;
						SendRequest(Rqid);
						//Envoi 6 requetes STOR vers les SFSP : / sending 3 request to obtains states of the 6 outputs
						Rqid= (type_STATE_S_TOR_1_TO_2<<SHIFT_TYPE_TRAME)+ BlocList_CAN[i].BlocID;
						SendRequest(Rqid);
						Rqid= (type_STATE_S_TOR_3_TO_4<<SHIFT_TYPE_TRAME)+ BlocList_CAN[i].BlocID;
						SendRequest(Rqid);
						Rqid= (type_STATE_S_TOR_5_TO_6<<SHIFT_TYPE_TRAME)+ BlocList_CAN[i].BlocID;
						SendRequest(Rqid);
						//Cr�ates 3 switch for Learning, Learning Exit and Clearing switches store into blocs
						
						std::string defaultname = NomRefBloc[RefBloc].c_str();
						std::string defaultnamenormal = defaultname + " LEARN EXIT";
						std::string defaultnamelearn = defaultname + " LEARN";
						std::string defaultnameclear = defaultname + " CLEAR";
						
						sID += (type_COMMANDE_ETAT_BLOC<<SHIFT_TYPE_TRAME);
						
						InsertUpdateControlSwitch(sID, BLOC_STATES_LEARNING_STOP, defaultnamenormal.c_str() );
						InsertUpdateControlSwitch(sID, BLOC_STATES_LEARNING, defaultnamelearn.c_str() );
						InsertUpdateControlSwitch(sID, BLOC_STATES_CLEARING, defaultnameclear.c_str() );
						break;
				}
				break;
			}
		}
	}
}

void USBtin_MultiblocV8::InsertUpdateControlSwitch(const int NodeID, const int ChildID, const std::string &defaultname ){
	
	//make device ID
	unsigned char ID1 = (unsigned char)((NodeID & 0xFF000000) >> 24);
	unsigned char ID2 = (unsigned char)((NodeID & 0xFF0000) >> 16);
	unsigned char ID3 = (unsigned char)((NodeID & 0xFF00) >> 8);
	unsigned char ID4 = (unsigned char)NodeID & 0xFF;

	char szIdx[10];
	sprintf(szIdx, "%X%02X%02X%02X", ID1, ID2, ID3, ID4);
	std::vector<std::vector<std::string> > result;
	result = m_sql.safe_query("SELECT Name,nValue,sValue FROM DeviceStatus WHERE (HardwareID==%d) AND (DeviceID=='%q') AND (Unit == %d) AND (Type==%d) AND (Subtype==%d)",
		m_HwdID, szIdx, ChildID, int(pTypeLighting2), int(sTypeAC) );
		
	if (result.empty())
	{
		m_sql.safe_query(
		"INSERT INTO DeviceStatus (HardwareID, DeviceID, Unit, Type, SubType, SwitchType, Used, SignalLevel, BatteryLevel, Name, nValue, sValue) "
		"VALUES (%d,'%q',%d,%d,%d,%d,0, 12,255,'%q',0,' ')",
		m_HwdID, szIdx, ChildID, pTypeLighting2, sTypeAC, int(STYPE_PushOn), defaultname.c_str() );
	}
}

//call every 3 sec...
void  USBtin_MultiblocV8::BlocList_CheckBloc(){
	int i;
	int RefBlocAlive = 0;
	unsigned long Rqid = 0;
	for(i = 0;i < MAX_NUMBER_BLOC;i++){
		if( BlocList_CAN[i].BlocID != 0 ){ //Si pr�sence d'un ID
			//we extract the blocs reference :
			RefBlocAlive = (( BlocList_CAN[i].BlocID & MSK_INDEX_MODULE) >> SHIFT_INDEX_MODULE);
			//and check the bloc state :
			if( BlocList_CAN[i].Status == BLOC_NOTALIVE ){
				//le bloc a �t� perdu / bloc lost...
				_log.Log(LOG_ERROR,"MultiblocV8: Bloc Lost with ref #%d# ",RefBlocAlive);
			}
			else{ //le bloc est en vie / Alive !
				//on v�rifie si il y'a des requetes automatique � envoyer : / checking if we must send request
				//_log.Log(LOG_NORM,"MultiblocV8: BlocAlive with ref #%d# ",RefBlocAlive);
				switch(RefBlocAlive){
					case BLOC_SFSP_M :
					case BLOC_SFSP_E :
						if( BOOL_TaskAGo == true ){
							Rqid= (type_E_ANA_1_TO_4<<SHIFT_TYPE_TRAME)+ BlocList_CAN[i].BlocID;
							SendRequest(Rqid);
						}
						if( BOOL_TaskRqStorGo == true ){
							BlocList_CAN[i].ForceUpdateSTOR[0] = true;
							BlocList_CAN[i].ForceUpdateSTOR[1] = true;
							BlocList_CAN[i].ForceUpdateSTOR[2] = true;
							BlocList_CAN[i].ForceUpdateSTOR[3] = true;
							BlocList_CAN[i].ForceUpdateSTOR[4] = true;
							BlocList_CAN[i].ForceUpdateSTOR[5] = true;
							Rqid= (type_STATE_S_TOR_1_TO_2<<SHIFT_TYPE_TRAME)+ BlocList_CAN[i].BlocID;
							SendRequest(Rqid);
							Rqid= (type_STATE_S_TOR_3_TO_4<<SHIFT_TYPE_TRAME)+ BlocList_CAN[i].BlocID;
							SendRequest(Rqid);
							Rqid= (type_STATE_S_TOR_5_TO_6<<SHIFT_TYPE_TRAME)+ BlocList_CAN[i].BlocID;
							SendRequest(Rqid);
						}
						break;
				}
				BlocList_CAN[i].Status = BLOC_NOTALIVE; //RAZ ici de l'info toutes les 3 sec !
			}
		}
	}
	BOOL_TaskAGo = false;
	BOOL_TaskRqStorGo = false;
}

//traitement sur r�ception trame de vie / Life frame treatment :
void USBtin_MultiblocV8::Traitement_Trame_Vie(const unsigned char RefBloc, const char Codage, const char Ssreseau,unsigned int rxDLC,unsigned int bufferdata[8])
{
	//treatment only for a frame of 5 bytes receive !
	if( rxDLC == 5 ) BlocList_GetInfo(RefBloc,Codage,Ssreseau,bufferdata);
}

//check if an output has changed...
bool USBtin_MultiblocV8::CheckOutputChange(unsigned long sID,int OutputNumber,bool CdeReceive,int LevelReceive){
	char szDeviceID[10];
	int i;
	bool returnvalue = true;
	sprintf(szDeviceID,"%07X",(unsigned int)sID);
	std::vector<std::vector<std::string> > result;
	bool ForceUpdate = false;
	
	unsigned long StoreIdToFind = sID &(MSK_INDEX_MODULE+MSK_CODAGE_MODULE+MSK_SRES_MODULE);
	//serching for the bloc :
	for(i = 0;i < MAX_NUMBER_BLOC;i++){
		if( BlocList_CAN[i].BlocID == StoreIdToFind ){
			//bloc trouv� on v�rifie si on doit forcer l'update ou non des composants associ�s :
			//bloc find, check if update is necessary :
			ForceUpdate = BlocList_CAN[i].ForceUpdateSTOR[OutputNumber];
			BlocList_CAN[i].ForceUpdateSTOR[OutputNumber] = false; //RAZ ici
			break;
		}
	}
	
	result = m_sql.safe_query("SELECT ID,nValue,sValue FROM DeviceStatus WHERE (HardwareID==%d) AND (DeviceID=='%q') AND (Type==%d) AND (Subtype==%d==%d) AND (Unit==%d)", 
		m_HwdID, szDeviceID,pTypeLighting2,sTypeAC, OutputNumber); //Unit = 1 = sortie n�1
	if(!result.empty() && !ForceUpdate ){ //if output exist in db and no forceupdate
		//check if we have a change, if not do not update it
		int nvalue = atoi(result[0][1].c_str());
		//_log.Log(LOG_NORM,"MultiblocV8: Output 1 nvalue : %d ",nvalue);
		if ( (!CdeReceive) && (nvalue == 0) ) returnvalue = false; //still off, nothing to do
		else{
			//Check Level changed
			int slevel = atoi(result[0][2].c_str());
			//_log.Log(LOG_NORM,"MultiblocV8: Output 1 slevel : %d ",slevel);
			if( (slevel == LevelReceive) ) returnvalue = false;
		}
	}
	return returnvalue;
}

//store information outputs new states...
void USBtin_MultiblocV8::OutputNewStates(unsigned long sID,int OutputNumber,bool Comandeonoff,int Level ){
	double rlevel = (15.0 / 255)*Level;
	int level = int(rlevel);
	//Extract the RefBloc Type
	char RefBloc = (sID & MSK_INDEX_MODULE) >> SHIFT_INDEX_MODULE;
	
	tRBUF lcmd;
	memset(&lcmd, 0, sizeof(RBUF));
	lcmd.LIGHTING2.packetlength = sizeof(lcmd.LIGHTING2) - 1;
	lcmd.LIGHTING2.packettype = pTypeLighting2;
	lcmd.LIGHTING2.subtype = sTypeAC;
	lcmd.LIGHTING2.id1 = (sID>>24)&0xff;
	lcmd.LIGHTING2.id2 = (sID>>16)&0xff;
	lcmd.LIGHTING2.id3 = (sID>>8)&0xff;
	lcmd.LIGHTING2.id4 = sID&0xff;
	
	if ( Comandeonoff == true ) lcmd.LIGHTING2.cmnd = light2_sOn;
	else lcmd.LIGHTING2.cmnd = light2_sOff;
		
	lcmd.LIGHTING2.unitcode = OutputNumber & 0xff; //SubUnit;	//output number
	lcmd.LIGHTING2.level = level; //level_value;		
	lcmd.LIGHTING2.filler = 2;
	lcmd.LIGHTING2.rssi = 12;
	//default name creation :
	std::string defaultname=NomRefBloc[RefBloc].c_str();
	defaultname += " output S";
	std::ostringstream convert;   // stream used for the conversion
	convert << OutputNumber;
	defaultname += convert.str();
	
	sDecodeRXMessage(this, (const unsigned char *)&lcmd.LIGHTING2, defaultname.c_str(), 255);
}

void USBtin_MultiblocV8::DoBlinkOutput(){
	//every one second, check if an output is On and in blink Mode
	int i = 0;
	int output_index = 0;
	int RefBlocAlive = 0;
	unsigned long sID = 0;
	
	BOOL_Global_BlinkOutputs = !BOOL_Global_BlinkOutputs;
	//serching for blocks :
	for(i = 0;i < MAX_NUMBER_BLOC;i++){
		if( BlocList_CAN[i].BlocID != 0 ){ //if bloc is logged
			//we extract the blocs reference :
			RefBlocAlive = (( BlocList_CAN[i].BlocID & MSK_INDEX_MODULE) >> SHIFT_INDEX_MODULE);
			if( BlocList_CAN[i].Status == BLOC_ALIVE ){ //only if block is alive
				switch(RefBlocAlive){ //Switch because the Number of output can be different for over ref blocks !
					case BLOC_SFSP_M :
					case BLOC_SFSP_E :
					//6 outputs on sfsp blocks
					for(output_index = 1;output_index < 7;output_index ++){
						if( BlocList_CAN[i].IsOutputBlink[output_index] == true ){
							_log.Log(LOG_NORM,"MultiblocV8: Output n: %d blink state %d",output_index,BOOL_Global_BlinkOutputs);
							if(output_index == 1 || output_index==2 ){
								sID = (type_STATE_S_TOR_1_TO_2<<SHIFT_TYPE_TRAME)+ BlocList_CAN[i].BlocID;
								//if Blink mode is on:
								//simulate a change only in domoticz, no sending frame for that !
								OutputNewStates( sID, output_index,BOOL_Global_BlinkOutputs,15 );
							}
							else if(output_index == 3 || output_index==4 ){
								sID = (type_STATE_S_TOR_3_TO_4<<SHIFT_TYPE_TRAME)+ BlocList_CAN[i].BlocID;
								//if Blink mode is on:
								//simulate a change only in domoticz, no sending frame for that !
								OutputNewStates( sID, output_index,BOOL_Global_BlinkOutputs,15 );
							}
							else if(output_index == 5 || output_index==6 ){
								sID = (type_STATE_S_TOR_5_TO_6<<SHIFT_TYPE_TRAME)+ BlocList_CAN[i].BlocID;
								//if Blink mode is on:
								//simulate a change only in domoticz, no sending frame for that !
								OutputNewStates( sID, output_index,BOOL_Global_BlinkOutputs,15 );
							}
							
						}
					}
					break;
					
				}
			}
		}
	}
}

//The STOR Frame always contain a maximum of 2 stor States. 4 Low bytes = STOR 1 / 4 high bytes = STOR2 
//( etc... for STOR3-4 ///  5/6  /// 7/8 ...)
void USBtin_MultiblocV8::Traitement_Etat_S_TOR_Recu(const unsigned int FrameType,const unsigned char RefeBloc, const char Codage, const char Ssreseau,unsigned int bufferdata[8])
{
	//char Subtype = 0;
	int level_value = 0;
	unsigned long sID = (FrameType<<SHIFT_TYPE_TRAME)+(RefeBloc<<SHIFT_INDEX_MODULE)+(Codage<<SHIFT_CODAGE_MODULE)+Ssreseau;
	bool OutputCde;
	bool IsBlink;
	//bool OutputPWM;
	
	switch(FrameType){
		case type_STATE_S_TOR_1_TO_2:
			if(bufferdata[2] & 0x01) OutputCde = true;
			else OutputCde = false;
			
			//if( ((bufferdata[2]>>4) & 0x01) )OutputPWM = true;
			//else OutputPWM = false;
			level_value = bufferdata[0];			//variable niveau (lvl) 0-254 � convertir en 0 - 100%
			level_value /= 16;
			if( level_value > 15 ) level_value = 15;
			if( CheckOutputChange(sID,1,OutputCde,level_value) == true ) { //on met � jours si n�cessaire !
				OutputNewStates( sID, 1,OutputCde,level_value );
			}
			
			if(bufferdata[2] & 0x02) IsBlink = true;
			else IsBlink = false;
			//SetOutputBlinkInDomoticz( sID,1,IsBlink);
			
			if(bufferdata[6] & 0x01) OutputCde = true;
			else OutputCde = false;
			
			//if((bufferdata[6]>>4) & 0x01)OutputPWM = true;
			//else OutputPWM = false;
			level_value = bufferdata[4];			//variable niveau (lvl) 0-254 � convertir en 0 - 100%
			level_value /= 16;
			if( level_value > 15 ) level_value = 15;
			if( CheckOutputChange(sID,2,OutputCde,level_value) == true ) { //on met � jours si n�cessaire !
				OutputNewStates( sID, 2,OutputCde,level_value );
			}
			
			if(bufferdata[6] & 0x02) IsBlink = true;
			else IsBlink = false;
			//SetOutputBlinkInDomoticz( sID,2,IsBlink);
			break;
			
		case type_STATE_S_TOR_3_TO_4:
			if(bufferdata[2] & 0x01) OutputCde = true;
			else OutputCde = false;
			//if( ((bufferdata[2]>>4) & 0x01) )OutputPWM = true;
			//else OutputPWM = false;
			level_value = bufferdata[0];			//variable niveau (lvl) 0-254 � convertir en 0 - 100%
			level_value /= 16;
			if( level_value > 15 ) level_value = 15;
			if( CheckOutputChange(sID,3,OutputCde,level_value) == true ) { //on met � jours si n�cessaire !
				OutputNewStates( sID, 3,OutputCde,level_value );
			}
			
			if(bufferdata[2] & 0x02) IsBlink = true;
			else IsBlink = false;
			//SetOutputBlinkInDomoticz( sID,3,IsBlink);
			
			if(bufferdata[6] & 0x01) OutputCde = true;
			else OutputCde = false;
			//if((bufferdata[6]>>4) & 0x01)OutputPWM = true;
			//else OutputPWM = false;
			level_value = bufferdata[4];			//variable niveau (lvl) 0-254 � convertir en 0 - 100%
			level_value /= 16;
			if( level_value > 15 ) level_value = 15;
			if( CheckOutputChange(sID,4,OutputCde,level_value) == true ) { //on met � jours si n�cessaire !
				OutputNewStates( sID, 4,OutputCde,level_value );
			}
			
			if(bufferdata[6] & 0x02) IsBlink = true;
			else IsBlink = false;
			//SetOutputBlinkInDomoticz( sID,4,IsBlink);
			break;
			
		case type_STATE_S_TOR_5_TO_6:
			if(bufferdata[2] & 0x01) OutputCde = true;
			else OutputCde = false;
			//if( ((bufferdata[2]>>4) & 0x01) )OutputPWM = true;
			//else OutputPWM = false;
			level_value = bufferdata[0];			//variable niveau (lvl) 0-254 � convertir en 0 - 100%
			level_value /= 16;
			if( level_value > 15 ) level_value = 15;
			if( CheckOutputChange(sID,5,OutputCde,level_value) == true ) { //on met � jours si n�cessaire !
				OutputNewStates( sID, 5,OutputCde,level_value );
			}
			
			if(bufferdata[2] & 0x02) IsBlink = true;
			else IsBlink = false;
			//SetOutputBlinkInDomoticz( sID,5,IsBlink);
			
			if(bufferdata[6] & 0x01) OutputCde = true;
			else OutputCde = false;
			//if((bufferdata[6]>>4) & 0x01)OutputPWM = true;
			//else OutputPWM = false;
			level_value = bufferdata[4];			//variable niveau (lvl) 0-254 � convertir en 0 - 100%
			level_value /= 16;
			if( level_value > 15 ) level_value = 15;
			if( CheckOutputChange(sID,6,OutputCde,level_value) == true ) { //on met � jours si n�cessaire !
				OutputNewStates( sID, 6,OutputCde,level_value );
			}
			
			if(bufferdata[6] & 0x02) IsBlink = true;
			else IsBlink = false;
			//SetOutputBlinkInDomoticz( sID,6,IsBlink);
			break;
	}
	
	
}

void USBtin_MultiblocV8::SetOutputBlinkInDomoticz (unsigned long sID,int OutputNumber,bool Blink){
	int i;
	unsigned long StoreIdToFind = sID &(MSK_INDEX_MODULE+MSK_CODAGE_MODULE+MSK_SRES_MODULE);
	//serching for the bloc to :
	for(i = 0;i < MAX_NUMBER_BLOC;i++){
		if( BlocList_CAN[i].BlocID == StoreIdToFind ){
			//bloc trouv�:
			//we extract the blocs reference :
			int RefBlocAlive = (( BlocList_CAN[i].BlocID & MSK_INDEX_MODULE) >> SHIFT_INDEX_MODULE);
			switch(RefBlocAlive){ //Switch because the Number of output can be different for over ref blocks !
					case BLOC_SFSP_M :
					case BLOC_SFSP_E :
						//6 outputs on sfsp blocks //OutputNumber
						BlocList_CAN[i].IsOutputBlink[OutputNumber] = Blink;
					break;
			}
		}
	}

}

//traitement d'une trame info analogique re�ue
void USBtin_MultiblocV8::Traitement_E_ANA_Recu(const unsigned int FrameType,const unsigned char RefBloc, const char Codage, const char Ssreseau,unsigned int bufferdata[8])
{
	unsigned long sID = (RefBloc<<SHIFT_INDEX_MODULE)+(Codage<<SHIFT_CODAGE_MODULE)+Ssreseau;	
	
	if( BOOL_DebugInMultiblocV8 == true ) _log.Log(LOG_NORM,"MultiblocV8: receive ANA (alimentation) sfsp: D0: %d D1: %d# ", bufferdata[0], bufferdata[1]);
	int VoltageLevel = bufferdata[0];
	VoltageLevel <<= 8;
	VoltageLevel += bufferdata[1];
	//_log.Log(LOG_NORM,"MultiblocV8: receive ANA1 (alimentation) sfsp: #%d# ",VoltageLevel);
	int percent = ((VoltageLevel * 100) / 125);
	float voltage = (float)VoltageLevel/10;
	SendVoltageSensor(((sID>>8)&0xffff), (sID&0xff), percent, voltage, "SFSP Voltage");
}

//Envoi d'une trame suite � une commande dans domoticz...
//sending Frame in respons to a domoticz action :
bool USBtin_MultiblocV8::WriteToHardware(const char *pdata, const unsigned char length)
{
	const tRBUF *pSen = reinterpret_cast<const tRBUF*>(pdata);
	char szDeviceID[10];
	char DataToSend[16];
	unsigned char packettype = pSen->ICMND.packettype;
	
	if( packettype == pTypeLighting2 )
	{
		//light command
		long sID_EnBase;
		// R�cup�re l'info ID stock�e qui contient l'identifiation Rebloc+Codage+Ssr�seau du bloc)
		// retreive the ID information of the blocs :
		sID_EnBase = (pSen->LIGHTING2.id1<<24)+(pSen->LIGHTING2.id2<<16)+(pSen->LIGHTING2.id3<<8)+(pSen->LIGHTING2.id4);
		int FrameType =  ( sID_EnBase & MSK_TYPE_TRAME) >> SHIFT_TYPE_TRAME;
		int ReferenceBloc = ( sID_EnBase & MSK_INDEX_MODULE ) >> SHIFT_INDEX_MODULE;
		int Codage = ( sID_EnBase & MSK_CODAGE_MODULE ) >> SHIFT_CODAGE_MODULE;
		int Ssreseau = ( sID_EnBase & MSK_SRES_MODULE ) >> SHIFT_SRES_MODULE;
		// on est autoris� � envoyer les commandes STOR uniquement si le thread est actif
		if( m_thread ){
			//pour envoyer une commande STOR : // for sending STOR Frame :
			if( FrameType == type_STATE_S_TOR_1_TO_2 ||
				FrameType == type_STATE_S_TOR_3_TO_4 ||
				FrameType == type_STATE_S_TOR_5_TO_6 ||
				FrameType == type_STATE_S_TOR_7_TO_8 ||
				FrameType == type_STATE_S_TOR_9_TO_10 ||
				FrameType == type_STATE_S_TOR_11_TO_12 ){
				
				unsigned long sID = (type_CMD_S_TOR<<SHIFT_TYPE_TRAME)+ (sID_EnBase&(MSK_INDEX_MODULE+MSK_CODAGE_MODULE+MSK_SRES_MODULE));
				sprintf(szDeviceID,"%08X",(unsigned int)sID);
				unsigned int OutputNumber = (pSen->LIGHTING2.unitcode) - 1; //output number for command
				unsigned int Command = 0;
				unsigned int Reserve = 0;
				int iLevel=pSen->LIGHTING2.level;
				
				if (iLevel>15)
					iLevel=15;
				float fLevel=(255.0f/15.0f)*float(iLevel);
				if (fLevel>254.0f)
					fLevel=255.0f;
				iLevel=int(fLevel);
				
				if( pSen->LIGHTING2.cmnd == light2_sSetLevel ){
					Command = 0x11; //ON + SetLevel
				}
				else if( pSen->LIGHTING2.cmnd == light2_sOn ){
					Command = 0x01;
				}
				else{ //Off...
					Command = 0;
				}
				
				unsigned long LongDataToSend = (OutputNumber<<24)+(Command<<16)+(Reserve<<8)+iLevel;
				
				sprintf(DataToSend,"%08X",(unsigned int)LongDataToSend);
				std::string szTrameToSend = "T"; //
				szTrameToSend += szDeviceID;
				szTrameToSend += "4";
				szTrameToSend += DataToSend;
				if( BOOL_DebugInMultiblocV8 == true ) _log.Log(LOG_NORM,"MultiblocV8: Sending Frame: %s ",szTrameToSend.c_str() );
				writeFrame(szTrameToSend);
				return true;				
			}
			else if( FrameType == type_SFSP_SWITCH ){ 
				//Pas d'envoi des switch cr�� sur r�ception de trames, ce sont des switch r�el
				//no sending frame for switch created by the CAN, they are real switch not virtual ! it's like enocean
				//if this is a switch created in domoticz, send it on the CanBus !
				if( ReferenceBloc == BLOC_DOMOTICZ && Codage == 0 && Ssreseau == 0 ){
					if( pSen->LIGHTING2.cmnd == light2_sOn || pSen->LIGHTING2.cmnd == light2_sOff ){
						//use directly the baseId to send a "push on" command, the send "push off" is automatic:
						unsigned char CodeTouche = (pSen->LIGHTING2.unitcode);
						Sid_PushOff_ToSend = sID_EnBase;
						CodeTouchePushOff_ToSend = CodeTouche;
						CodeTouche |= 0x80; //send a "push ON" command
						USBtin_MultiblocV8_Send_SFSPSwitch_OnCAN(sID_EnBase,CodeTouche);
						BOOL_SendPushOffSwitch = true; //Auto push off switch because it works like EnOcean (Press and Released info on one switch).
						return true;
					}
					else if( pSen->LIGHTING2.cmnd == light2_sSetLevel ){
						//to do : if user set the level we must send the command By Outpu direct command and not by SFSP Frame
						_log.Log(LOG_ERROR,"MultiblocV8: Dimmer level not yet supported !");
						return false;
					}
				}
				else{ 
					_log.Log(LOG_ERROR,"MultiblocV8: Can not switch with this DeviceID,this is not a virtual switch...");
					return false;	
				}
			}
			else if( FrameType == type_COMMANDE_ETAT_BLOC ){ //specific command send to blocs
				if( ReferenceBloc == BLOC_SFSP_M || ReferenceBloc == BLOC_SFSP_E ){
					//on each "push on" switch send a bloc command starting with learn on OUTPUT 1
					//to OUTPUT 6 and return to Normal State Bloc.
					//In learning mode the output return states : ON + BLINK(1sec) ON but we can't show this in domoticz on a SWITCH because the output will only turn on !
					//So we can show it in the log message (OUTPUT+n� + LEARN MODE) !
					unsigned char Commande = (pSen->LIGHTING2.unitcode);
					switch(Commande){
						default :
						case BLOC_STATES_LEARNING_STOP :
							CommandBlocToSend = BLOC_STATES_LEARNING_STOP;
						break;
						case BLOC_STATES_LEARNING	:
							CommandBlocToSend = BLOC_STATES_LEARNING;
						break;
						case BLOC_STATES_CLEARING	:
							CommandBlocToSend = BLOC_STATES_CLEARING;
						break;
					}
					//sID_EnBase + commande 
					USBtin_MultiblocV8_Send_CommandBlocState_OnCAN(sID_EnBase,CommandBlocToSend);
					if( Commande == BLOC_STATES_LEARNING ){
						USBtin_MultiblocV8_Send_SFSP_LearnCommand_OnCAN(sID_EnBase,0); //
					}
					return true;
				}
				else{
					_log.Log(LOG_ERROR,"MultiblocV8: Error Command BLoc not allowed !");
					return false;
				}
			}
		}
		else{
			_log.Log(LOG_ERROR,"MultiblocV8: This Can engine is disabled, please re-check MultiblocV8...");
		}
	}
	return false;
}

void USBtin_MultiblocV8::USBtin_MultiblocV8_Send_SFSPSwitch_OnCAN(long sID_ToSend,char CodeTouche){
	char szDeviceID[10];
	char DataToSend[16];
	sprintf(szDeviceID,"%08X",(unsigned int)sID_ToSend);
	//unsigned int DevIdOnCan = 0x00000001; //on the CAN a wired Input always send a DevId at 0x01 on a u32
	//differ from a real wireless EnOcean receive switch send directly its (u32)DeviceId 
	sprintf(DataToSend,"%02X",(unsigned char)CodeTouche);
	std::string szTrameToSend = "T"; //
	szTrameToSend += szDeviceID;
	szTrameToSend += "5";		 //DLC always to 5 for SFSP_SWITCH Frame
	szTrameToSend += "00000001"; //always set to 1 because it's an internal switch (from domoticz)
	szTrameToSend += DataToSend; //contain data code + send On information
	//if( BOOL_DebugInMultiblocV8 == true ) _log.Log(LOG_NORM,"MultiblocV8: Sending Frame: %s ",szTrameToSend.c_str() );
	writeFrame(szTrameToSend);
}

void USBtin_MultiblocV8::USBtin_MultiblocV8_Send_CommandBlocState_OnCAN(long sID_ToSend,char Commande){
	char szDeviceID[10];
	char DataToSend[16];
	sprintf(szDeviceID,"%08X",(unsigned int)sID_ToSend);
	//unsigned int DevIdOnCan = 0x00000001; //on the CAN a wired Input always send a DevId at 0x01 on a u32
	//differ from a real wireless EnOcean receive switch send directly its (u32)DeviceId 
	sprintf(DataToSend,"%02X",(unsigned char)Commande);
	std::string szTrameToSend = "T"; //
	szTrameToSend += szDeviceID;
	szTrameToSend += "1";		 //DLC always to 5 for SFSP_SWITCH Frame
	szTrameToSend += DataToSend; //contain data code + send On information
	//if( BOOL_DebugInMultiblocV8 == true ) 
	_log.Log(LOG_NORM,"MultiblocV8: Sending BlocState command: %s ",szTrameToSend.c_str() );
	writeFrame(szTrameToSend);
}

void USBtin_MultiblocV8::USBtin_MultiblocV8_Send_SFSP_LearnCommand_OnCAN(long baseID_ToSend,char Commande){
	char szDeviceID[10];
	char DataToSend[16];
	unsigned long sID = (type_CMD_S_TOR<<SHIFT_TYPE_TRAME)+ (baseID_ToSend&(MSK_INDEX_MODULE+MSK_CODAGE_MODULE+MSK_SRES_MODULE));
	sprintf(szDeviceID,"%08X",(unsigned int)sID);
	//unsigned int DevIdOnCan = 0x00000001; //on the CAN a wired Input always send a DevId at 0x01 on a u32
	//differ from a real wireless EnOcean receive switch send directly its (u32)DeviceId 
	sprintf(DataToSend,"%02X",(unsigned char)Commande);
	std::string szTrameToSend = "T"; //
	szTrameToSend += szDeviceID;
	szTrameToSend += "1";		 //DLC always to 5 for SFSP_SWITCH Frame
	szTrameToSend += DataToSend; //contain data code + send On information
	//if( BOOL_DebugInMultiblocV8 == true ) 
	_log.Log(LOG_NORM,"MultiblocV8: Sending SFSP learn command: %s ",szTrameToSend.c_str() );
	writeFrame(szTrameToSend);
}


