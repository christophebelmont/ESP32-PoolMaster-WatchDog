/**
 * ----------------------------------------------------------------------------
 * ESP32 Remote Control with WebSocket
 * ----------------------------------------------------------------------------
 * © 2020 Stéphane Calderoni
 * ----------------------------------------------------------------------------
 */

var gateway = `ws://${window.location.hostname}/ws`;
var websocket;

// ----------------------------------------------------------------------------
// Initialization
// ----------------------------------------------------------------------------

window.addEventListener('load', onLoad);

function onLoad(event) {
    initWebSocket();
    initButton();
}

// ----------------------------------------------------------------------------
// WebSocket handling
// ----------------------------------------------------------------------------

function initWebSocket() {
    console.log('Trying to open a WebSocket connection...');
    websocket = new WebSocket(gateway);
    websocket.onopen    = onOpen;
    websocket.onclose   = onClose;
    websocket.onmessage = onMessage;
}

function onOpen(event) {
    console.log('Connection opened');
}

function onClose(event) {
    console.log('Connection closed');
    setTimeout(initWebSocket, 2000);
}

function onMessage(event) {
    let data = JSON.parse(event.data);
    document.getElementById('led').className = data.status;
}

// ----------------------------------------------------------------------------
// Button handling
// ----------------------------------------------------------------------------

function initButton() {
    document.getElementById('on').addEventListener('click', onTurnOn);
    document.getElementById('off').addEventListener('click', onTurnOff);
    document.getElementById('nextion').addEventListener('click', onUpGradeNextion);
    document.getElementById('poolmaster').addEventListener('click', onUpGradePoolMaster);
}

function onTurnOn(event) {
    websocket.send(JSON.stringify({'action':'on'}));
}
function onTurnOff(event) {
    websocket.send(JSON.stringify({'action':'off'}));
}
function onUpGradeNextion(event) {
    websocket.send(JSON.stringify({'action':'nextion'}));
}
function onUpGradePoolMaster(event) {
    websocket.send(JSON.stringify({'action':'poolmaster'}));
}