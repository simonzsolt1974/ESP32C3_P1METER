// P1 METER BASIC CONFIGURATION
// meterType:
//   0 = NO METER
//   1 = SAGEMCOM XS210_D ESMR5
//   2 = LANDIS GYR E350 ZMF100
//   3 = SANXING SX631 / S34U18

String webPage;

const char BASISCONFIG[] PROGMEM = R"=====(
<body>
<div id='msect'>
<div id="menu">
<a href="#" id="sub" onclick='submitFunction()'>save</a>
<a href="#" class='close' onclick='cl();'>&times;</a>
</div>
</div>

<div id='msect'><kop>P1-METER SETTINGS</kop></div>

<div id='msect'>
  <div class='divstijl' style='width: 480px; height:56vh;'>
  <form id='formulier' method='get' action='submitform' oninput='showSubmit()'>
  <center><table>
    <tr><td>user passwd<td><input class='inp5' name='pw1' length='11' placeholder='max. 10 char' value='{pw1}' pattern='.{4,10}' title='between 4 en 10 characters'></input>
    </td></tr>

    <tr><td class="cap">meter model<td><select name='mtype' class='sb1' id='sel'>
      <option value='0' mtype_0>NO METER</option>
      <option value='1' mtype_1>SAGEMCOM XS210_D ESMR5</option>
      <option value='2' mtype_2>LANDIS GYR E350 ZMF100</option>
      <option value='3' mtype_3>SANXING SX631 / S34U18</option>
    </select></tr>

    <tr><td>baud 9600<td><input type='checkbox' style='width:30px; height:30px;' name='baud' #sjik></input></td></tr>
    <tr><td>test<td><input type='checkbox' style='width:30px; height:30px;' name='tst' #sjuk></input></td></tr>
    <tr><td>rxInvert:<td><input type='checkbox' style='width:30px; height:30px;' name='rxI' #check></input></td></tr>
    <tr><td>3phase meter:<td><input type='checkbox' style='width:30px; height:30px;' name='3ph' #sjak></input></td></tr>

    <tr><td class="cap">polling frequency<td><select name='pfreq' class='sb1' id='sel2'>
      <option value='0' pfreq_0>no polling</option>
      <option value='30' pfreq_1>every 30 sec</option>
      <option value='60' pfreq_2>every 1 min</option>
      <option value='300' pfreq_3>every 5 min</option>
    </select></tr>

    <tr><td>serial debug:<td><input type='checkbox' style='width:30px; height:30px;' name='debug' #sjek></input></td></tr>
  </table></form>
  </table>
  </div><br>
</div>
</body></html>
)=====";

void zendPageBasis(AsyncWebServerRequest *request)
{
  webPage = FPSTR(HTML_HEAD);
  webPage += FPSTR(BASISCONFIG);

  webPage.replace("'{pw1}'", "'" + String(userPwd) + "'");

  if (baudRate9600) webPage.replace("#sjik", "checked");
  if (threePhase)   webPage.replace("#sjak", "checked");
  if (bootTest)     webPage.replace("#sjuk", "checked");
  if (rxInvert)     webPage.replace("#check", "checked");
  if (diagNose)     webPage.replace("#sjek", "checked");

  switch (meterType)
  {
    case 0:
      webPage.replace("mtype_0", "selected");
      break;
    case 1:
      webPage.replace("mtype_1", "selected");
      break;
    case 2:
      webPage.replace("mtype_2", "selected");
      break;
    case 3:
      webPage.replace("mtype_3", "selected");
      break;
    default:
      meterType = 0;
      webPage.replace("mtype_0", "selected");
      break;
  }

  switch (pollFreq)
  {
    case 0:
      webPage.replace("pfreq_0", "selected");
      break;
    case 30:
      webPage.replace("pfreq_1", "selected");
      break;
    case 60:
      webPage.replace("pfreq_2", "selected");
      break;
    case 300:
      webPage.replace("pfreq_3", "selected");
      break;
    default:
      pollFreq = 0;
      webPage.replace("pfreq_0", "selected");
      break;
  }

  request->send(200, "text/html", webPage);
  webPage = "";
}
