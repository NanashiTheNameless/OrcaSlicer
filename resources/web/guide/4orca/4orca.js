
// The user's telemetry choice, kept while Stealth Mode forces the checkbox off.
var m_bTelemetry=true;
// "registry" or "environment" when an opt-out outside the settings turns telemetry off whatever the user picks here.
var m_sTelemetryOptOut="";

function OnInit()
{
	TranslatePage();
	
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="request_userguide_profile";
	
	SendWXMessage( JSON.stringify(tSend) );
}

function HandleStudio(pVal)
{
	if(pVal['command']!='response_userguide_profile')
		return;
	
	let pProfile=pVal['response'];
	
	$('#StealthMode').prop('checked', pProfile["stealth_mode"]==true);
	m_bTelemetry=pProfile["telemetry"]!=false;
	m_sTelemetryOptOut=pProfile["telemetry_opt_out"] || "";
	
	// Same check as the filament page: offer the plug-in page only when it is needed.
	if(pProfile["network_plugin_install"]=='1' && pProfile["network_plugin_compability"]=='1')
	{
		$("#GotoNetPluginBtn").hide();
		$("#AcceptBtn").show();
	}
	
	SendStealthModeCheck();
	SendTelemetryCheck();
}

function SendStealthModeCheck()
{
	let bStealth=$('#StealthMode').is(':checked');
	let bForcedOff=bStealth || m_sTelemetryOptOut!="";
	
	$('#Telemetry').prop('disabled', bForcedOff);
	$('#Telemetry').prop('checked', !bForcedOff && m_bTelemetry);
	$('#TelemetryCheckArea').toggleClass('Disabled', bForcedOff);
	
	// Say why the checkbox is off; a system opt-out outlasts Stealth Mode, so it wins.
	$('#TelemetryOffRegistry').toggle(m_sTelemetryOptOut=="registry");
	$('#TelemetryOffEnvironment').toggle(m_sTelemetryOptOut=="environment");
	$('#TelemetryOffStealth').toggle(m_sTelemetryOptOut=="" && bStealth);
	
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="save_stealth_mode";
	tSend['data']={};
	tSend['data']['action']=bStealth ? "yes" : "no";
	
	SendWXMessage( JSON.stringify(tSend) );

	return true;
}

function SendTelemetryCheck()
{
	if( !$('#Telemetry').is(':disabled') )
		m_bTelemetry=$('#Telemetry').is(':checked');
	
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="save_telemetry";
	tSend['data']={};
	tSend['data']['action']=m_bTelemetry ? "yes" : "no";
	
	SendWXMessage( JSON.stringify(tSend) );

	return true;
}

function GotoNetPluginPage()
{
	let bRet=SendStealthModeCheck() && SendTelemetryCheck();
	
	if(bRet)
		window.location.href="../5/index.html";
}


function FinishGuide()
{
	SendStealthModeCheck();
	SendTelemetryCheck();
	
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="user_guide_finish";
	tSend['data']={};
	tSend['data']['action']="finish";
	
	SendWXMessage( JSON.stringify(tSend) );	
	
	//window.location.href="../6/index.html";
}
