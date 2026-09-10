
//<input type="number" id="invCount" value="%count%" hidden>
//<script type="text/javascript" src="SECURITY"></script>
// <li><a href='/LOGPAGE'>log</a></li>
  //<li><a href='/INFOPAGE'>info</a></li>  

//  <li><a href='/ABOUT'>about</a></li>
//  <li><a href='/LOGPAGE'>journal</a></li>
  
//  <li id="ml" style='float:right; display:none'><a id='haha' href='/MENU'>menu</a></li>
//<link rel="icon" type="image/x-icon" href="/favicon.ico" />
const char P1_HOMEPAGE[] PROGMEM = R"=====(
<!DOCTYPE html><html><head>

<meta charset='utf-8' name="viewport" content="width=device-width, initial-scale=1">
<title>ESP-P1 METER</title>
<link rel="icon" type="image/x-icon" href="/favicon.ico" />
<link rel="stylesheet" type="text/css" href="/STYLESHEET">  


<style>
body {
 background-color: #EEE;
}
span {
  padding: 6px;
}
table, th, td {
  border: 2px solid blue; font-size:16px; padding:6px; text-align:center; border-collapse:collapse;backgound-color:#dfff80;
  }
tr {background-color:#ccffcc;}
//td { width:70px; }

#td0 {width:130px;}
#td1 {width:110px;}
#td2 {width:40px;}
#td3 {width:70px;}


.btn {
  background-color: #199319;
  color: white;
  padding: 5px 22px;
  border-radius:6px;
}

.btn:hover {background: #eeeF; color:black;}

/* Popup container - can be anything you want */
.popup {
  position: relative;
  display: inline-block;
  cursor: pointer;
  -webkit-user-select: none;
  -moz-user-select: none;
  -ms-user-select: none;
  user-select: none;
}

/* The actual popup */
.popup .popuptext {
  visibility: hidden;
  width: 200px;
  background-color: #090963;
  color: #fff;
  text-align: left;
  border-radius: 6px;
  padding: 8px 0;
  padding-left:20px;
  position: absolute;
  z-index: 1;
  bottom: 125%;
  left: 0%;
  margin-left: -80px;
}

/* Popup arrow */
.popup .popuptext::after {
  content: "";
  position: absolute;
  top: 100%;
  left: 70%;
  margin-left: -5px;
  border-width: 5px;
  border-style: solid;
  border-color: #555 transparent transparent transparent;
}

/* Toggle this class - hide and show the popup */
.popup .show {
  visibility: visible;
  -webkit-animation: fadeIn 1s;
  animation: fadeIn 1s;
}

/* Add animation (fade in the popup) */
@-webkit-keyframes fadeIn {
  from {opacity: 0;} 
  to {opacity: 1;}
}

@keyframes fadeIn {
  from {opacity: 0;}
  to {opacity:1 ;}
}

@media only screen and (max-width: 800px) {
th, td { font-size:11px; }
#td0 {width:90px}
#td1 {width:100px;}
#td2 {width:40px;}
#td3 {width:80px}
.popup .popuptext {width: 160px;}

tr {height:35px;} 
.btn { padding: 5px 18px; font-size:10px;}
}
</style>

<script type="text/javascript" src="SECURITY"></script>
</head>

<body onload='loadScript()'>

<div id='msect'>
<div id='menu'>
<a id="ml" href='/MENU' style='float:right; display:none'>menu</a>
</div>
</div>
<div id='msect'>
  <kop>ESP32C3 P1 METER</kop>
</div>
<div id='msect'>
    <div class='divstijl' id='maindiv'>
        <center>
        
        <h4>VILLANY MÉRŐ @ <span id="time"></span></h4>
        
        <div id='noOutput' style='display:block'>
          <h4 style='color:red'>waiting for output</h4>
        </div>
        
        <div id="4channel">
          <center><table>
          <tr style='Background-color:lightblue; font-weight:bold; text-align:center; border:4px solid black;'>
          <td id="td0">MEDIUM<td id="td1">AMOUNT<td id="td2">UNIT<td id="td3">DETAILS</tr>
          <tr><td>Összes fogyasztás</td><td id="p01"></td><td>kWh</td><td><div class='popup'><button class='btn' id='btn1'>details</button><span class='popuptext' id='myPopup'></span><div></tr>
          <tr><td>Összes visszatáplálás</td><td id="p11"></td><td>kWh</td><td><button class='btn' id='btn2'>details</button></tr>
          <tr><td>Teljesítmény L1</td><td id="p21"></td><td>W</td><td><button class='btn' id='btn3'>details</button></tr>
          <tr><td>Teljesítmény L2</td><td id="p22"></td><td>W</td><td><button class='btn' id='btn4'>details</button></tr>
          <tr><td>Teljesítmény L3</td><td id="p23"></td><td>W</td><td><button class='btn' id='btn5'>details</button></tr>
          <tr style='font-weight:bold;'><td>Összes pillanatnyi teljesítmény</td><td id="p24"></td><td>W</td><td></td></tr>
        </table>
        </div>  
        <p>Powered by Hansiart</p>
             
        </center>
    </div>
<script type="text/javascript" src="JAVASCRIPT"></script>
</div></body>

</html>
)=====";

const char JAVA_SCRIPT[] PROGMEM = R"=====(

var btn1 = document.getElementById('btn1');
var btn2 = document.getElementById('btn2');
var btn3 = document.getElementById('btn3');
var btn4 = document.getElementById('btn4');
var btn5 = document.getElementById('btn5');
var popup = document.getElementById("myPopup");
var CON_HT;
var CON_LT;
var RET_HT;
var RET_LT;
// signed total instantaneous power in W (import +, export -)
var PWRPTOT = 0;
// per-phase signed power, null when neither direct meter registers nor
// the U x I x PF calculation can provide a value
var PWRP1 = null;
var PWRP2 = null;
var PWRP3 = null;
var phasePwr = false;
// source of the per-phase power values: "meter" (direct OBIS registers),
// "calculated" (U x I x PF) or null when unavailable
var phasePwrSrc = null;
var PWRC1 = 0, PWRR1 = 0, PWRC2 = 0, PWRR2 = 0, PWRC3 = 0, PWRR3 = 0;

function loadScript() {
 init() 
 getData();
}

function init() {
  btn1.addEventListener('click', pop1);
  btn2.addEventListener('click', pop2);
  btn3.addEventListener('click', pop3);
  btn4.addEventListener('click', pop4);
  btn5.addEventListener('click', pop5);
  document.removeEventListener('click', hidePopup);
}
function change() {
  btn1.removeEventListener('click', pop1);
  btn2.removeEventListener('click', pop2);
  btn3.removeEventListener('click', pop3);
  btn4.removeEventListener('click', pop4);
  btn5.removeEventListener('click', pop5);
  document.addEventListener('click', hidePopup);
}
function pop1() {
  var popup = document.getElementById("myPopup");
  popup.classList.toggle("show");
  popup.innerHTML="ÖSSZES FOGYASZTÁS<br>  magas tarifa : " + CON_HT + " kWh<br>  alacsony tarifa: " + CON_LT + " kWh";
  setTimeout(change,200);
}
function pop2() {
  var popup = document.getElementById("myPopup");
  popup.classList.toggle("show");
  popup.innerHTML="ÖSSZES VISSZATÁPLÁLÁS<br>  magas tarifa : " + RET_HT + " kWh<br>  alacsony tarifa: " + RET_LT + " kWh";
  setTimeout(change,200);
}
function pop3() {
  var popup = document.getElementById("myPopup");
  popup.classList.toggle("show");
  if (PWRP1 === null) {
    popup.innerHTML="TELJESÍTMÉNY L1<br>  nem elérhető (a mérő nem szolgáltat fázisteljesítményt)<br>  összes pillanatnyi teljesítmény : " + PWRPTOT + " W";
  } else if (phasePwrSrc == "calculated") {
    popup.innerHTML="TELJESÍTMÉNY L1 (SZÁMOLT)<br>  pillanatnyi teljesítmény : " + PWRP1 + " W<br>  számítás: U × I × cosφ (a mérő nem küld fázisteljesítményt)";
  } else {
    popup.innerHTML="TELJESÍTMÉNY L1<br>  fogyasztás : " + PWRC1 + " W<br>  visszatáplálás : " + (0 - PWRR1) + " W";
  }
  setTimeout(change,200);
}
function pop4() {
  var popup = document.getElementById("myPopup");
  popup.classList.toggle("show");
  if (PWRP2 === null) {
    popup.innerHTML="TELJESÍTMÉNY L2<br>  nem elérhető (a mérő nem szolgáltat fázisteljesítményt)<br>  összes pillanatnyi teljesítmény : " + PWRPTOT + " W";
  } else if (phasePwrSrc == "calculated") {
    popup.innerHTML="TELJESÍTMÉNY L2 (SZÁMOLT)<br>  pillanatnyi teljesítmény : " + PWRP2 + " W<br>  számítás: U × I × cosφ (a mérő nem küld fázisteljesítményt)";
  } else {
    popup.innerHTML="TELJESÍTMÉNY L2<br>  fogyasztás : " + PWRC2 + " W<br>  visszatáplálás : " + (0 - PWRR2) + " W";
  }
  setTimeout(change,200);
}
function pop5() {
  var popup = document.getElementById("myPopup");
  popup.classList.toggle("show");
  if (PWRP3 === null) {
    popup.innerHTML="TELJESÍTMÉNY L3<br>  nem elérhető (a mérő nem szolgáltat fázisteljesítményt)<br>  összes pillanatnyi teljesítmény : " + PWRPTOT + " W";
  } else if (phasePwrSrc == "calculated") {
    popup.innerHTML="TELJESÍTMÉNY L3 (SZÁMOLT)<br>  pillanatnyi teljesítmény : " + PWRP3 + " W<br>  számítás: U × I × cosφ (a mérő nem küld fázisteljesítményt)";
  } else {
    popup.innerHTML="TELJESÍTMÉNY L3<br>  fogyasztás : " + PWRC3 + " W<br>  visszatáplálás : " + (0 - PWRR3) + " W";
  }
  setTimeout(change,200);
}
function hidePopup(a) {
  var popup = document.getElementById("myPopup");
   //popup.style.visibility='hidden';
   popup.classList.toggle("show");
   init();
   //setTimeout(init,0);
   }

window.addEventListener('visibilitychange', () =>{
    if (document.visibilityState === 'visible') {
    //window.location.reload();}
    getData();
    }
})

function celbgc(cel) {
     document.getElementById(cel).style = "background-color:#c6ff1a";  
}
function celbga(cel) {
     document.getElementById(cel).style = "background-color:#f59b0a";  
}

function getData() {
  var xhttp = new XMLHttpRequest();
  xhttp.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      var antwoord = this.responseText;
      var obj = JSON.parse(antwoord);
//{"CON_HT":0,"CON_LT":51.775,"RET_HT":0,"RET_LT":24.413,"PWRPTOT":-3906,"phasePwr":false,"phasePwrSrc":"calculated","PWRP1":906,"PWRP2":1177,"PWRP3":1417,"enR":24.413,"enC":51.775,"rm":0}
// PWRP1..PWRP3 are present when the phase power is available from the
// meter (phasePwrSrc == "meter") or calculated U x I x PF
// (phasePwrSrc == "calculated"); they are absent when unavailable.
      CON_HT = obj.CON_HT;
      CON_LT = obj.CON_LT;
      RET_HT = obj.RET_HT;
      RET_LT = obj.RET_LT;
      // SIGNED instantaneous power in W straight from the meter telegram.
      // PWRPTOT is the meter's real total (1.7.0 - 2.7.0).
      // PWRP1..PWRP3 are per-phase values (meter or U x I x PF calculated);
      // they are null (unavailable) when neither source can provide them.
      PWRPTOT = obj.PWRPTOT;
      PWRP1 = (obj.PWRP1 === undefined) ? null : obj.PWRP1; 
      PWRP2 = (obj.PWRP2 === undefined) ? null : obj.PWRP2;      
      PWRP3 = (obj.PWRP3 === undefined) ? null : obj.PWRP3;
      phasePwr = obj.phasePwr;
      phasePwrSrc = (obj.phasePwrSrc === undefined) ? null : obj.phasePwrSrc;
      PWRC1 = obj.PWRC1 || 0; 
      PWRR1 = obj.PWRR1 || 0;    
      PWRC2 = obj.PWRC2 || 0; 
      PWRR2 = obj.PWRR2 || 0;      
      PWRC3 = obj.PWRC3 || 0; 
      PWRR3 = obj.PWRR3 || 0;      
      var enC = obj.enC;
      var enR = obj.enR;
      var rem = obj.rm;
      if(rem == 0) {document.getElementById("ml").style.display = "block";} // hide menu link         
      cel="p01";
      if(enC < 0 ) celbga(cel); else celbgc(cel);
      if(obj.enC != "n/a") {
           document.getElementById(cel).innerHTML = enC.toFixed(3);
           } else {
           document.getElementById(cel).innerHTML = "n/a";
           } 

      cel="p11";
      if(enR < 0 ) celbga(cel); else celbgc(cel);
      if(obj.enR != "n/a") {
           document.getElementById(cel).innerHTML = enR.toFixed(3);
           } else {
           document.getElementById(cel).innerHTML = "n/a";
           }      

      cel="p21"; //teljesítmény L1
      if (PWRP1 === null) {
        document.getElementById(cel).innerHTML = "n/a";
      } else {
        if(PWRP1 < 0 ) celbga(cel); else celbgc(cel);
        document.getElementById(cel).innerHTML =
          (phasePwrSrc == "calculated") ? PWRP1 + " (számolt)" : PWRP1;
      }
           
      cel="p22"; //teljesítmény L2
      if (PWRP2 === null) {
        document.getElementById(cel).innerHTML = "n/a";
      } else {
        if(PWRP2 < 0 ) celbga(cel); else celbgc(cel);
        document.getElementById(cel).innerHTML =
          (phasePwrSrc == "calculated") ? PWRP2 + " (számolt)" : PWRP2;
      }
 
      cel="p23"; //teljesítmény L3
      if (PWRP3 === null) {
        document.getElementById(cel).innerHTML = "n/a";
      } else {
        if(PWRP3 < 0 ) celbga(cel); else celbgc(cel);
        document.getElementById(cel).innerHTML =
          (phasePwrSrc == "calculated") ? PWRP3 + " (számolt)" : PWRP3;
      }

      // Összes pillanatnyi teljesítmény = the meter's real signed total
      // (mérőből = directly from the meter's 1.7.0 / 2.7.0 registers)
      if(PWRPTOT < 0 ) celbga("p24"); else celbgc("p24");
      document.getElementById("p24").innerHTML = PWRPTOT + " (mérőből)";
            

      document.getElementById("noOutput").style.display = "none";

      document.getElementById("time").innerHTML = obj.timestamp;
      }
  }
  xhttp.open("GET", "/get.Data", true);
  xhttp.send();
}


if (!!window.EventSource) {
 var source = new EventSource('/events');

 source.addEventListener('open', function(e) {
  console.log("Events Connected");
 }, false);

 source.addEventListener('error', function(e) {
  if (e.target.readyState != EventSource.OPEN) {
    console.log("Events Disconnected");
  }
 }, false);

 source.addEventListener('message', function(e) {
  console.log("message", e.data);
  if(e.data == "getall") {
  getData();
  }

 }, false);
}

)=====";
