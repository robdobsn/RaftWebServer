
function bodyIsLoaded() {

    // Setup page state
    showInElemByID("webSocket0State", "Not connected");
    showInElemByID("webSocket1State", "Not connected");
    window.pageState = {
        wsInfo: [{}, {}]
    };

    window.webSockets = [null, null];
    // Connect ws
    connectWs(0);
    connectWs(1);   
}

function connectWs(wsIdx)
{
    // Setup webSocket
    let wsElem = window.webSockets[wsIdx];
    if (!wsElem) 
    {
        console.log('Attempting WebSocket connection');
        wsElem = new WebSocket('ws://' + location.hostname + '/ws');
        wsElem.binaryType = "arraybuffer";

        // Websocket opened
        wsElem.onopen = function(evt) 
        {
            console.log(`WebSocket ${wsIdx} connection opened`);
            // wsElem.send("It's open! Hooray!!!");
            showInElemByID(`webSocket${wsIdx}State`, 'Open');

            // Set a timer to send a message repeatedly
            if (!window.pageState.wsInfo[wsIdx].count)
                window.pageState.wsInfo[wsIdx].count = 0;
            window.pageState.wsInfo[wsIdx].timer = setInterval(() => {
                const msg = `Hello from client ${wsIdx} count ${window.pageState.wsInfo[wsIdx].count++}\n`
                wsElem.send(msg)
                showInElemByID(`webSocket${wsIdx}SendMsg`, "Last tx: " + msg);
            }, 1000);

        }
        
        // Websocket message received
        wsElem.onmessage = function(evt) 
        {
            // Check for binary data
            let msgStr = '';
            if (evt.data instanceof ArrayBuffer)
            {
                // Convert to string
                const enc = new TextDecoder("utf-8");
                msgStr = enc.decode(evt.data);
            }
            else
            {
                msgStr = evt.data;
            }
                
            // Debug
            console.log(`WebSocket ${wsIdx} message received: ${msgStr}`);
            showInElemByID(`webSocket${wsIdx}RecvMsg`, "Last rx: " + msgStr);
            
            // window.pageState.martyIF.handleRxFrame(msg);
            
            // switch(msg.charAt(0)) 
            // {
            //     case 'L':
            //         console.log(msg);
            //         let value = parseInt(msg.replace(/[^0-9\.]/g, ''), 10);
            //         // slider.value = value;
            //         console.log("Led = " + value);
            //         break;

            //     case '{':
            //         console.log(msg);
            //         statusCallback(msg);
            //         document.getElementById("rxJson").innerHTML = msg;
            //         break;

            //     default:
            //         document.getElementById("rxJson").innerHTML = evt.data;
            //         break;
            // }
        }
        
        // Websocket closed
        wsElem.onclose = function(evt) 
        {
            clearInterval(window.pageState.wsInfo[wsIdx].timer);
            showInElemByID(`webSocket${wsIdx}State`, 'Closed');
            wsElem = null;
            if (window.pageState.wsInfo[wsIdx].reconnectTimer)
                clearTimeout(window.pageState.wsInfo[wsIdx].reconnectTimer);
            window.pageState.wsInfo[wsIdx].reconnectTimer = setTimeout(() =>
            {
                connectWs(wsIdx);
            }, 500);
        }
        
        // Websocket error
        wsElem.onerror = function(evt) 
        {
            console.log('Websocket error: ' + evt);
            showInElemByID(`webSocket${wsIdx}State`, 'Error');
        }
          
        // let source = new webSocket('/events');
        // // Events on async event source
        // source.addEventListener('open', function(e) {
        //     console.log("Async events connected");
        // }, false);
        // source.addEventListener('error', function(e) {
        //     if (e.target.readyState != webSocket.OPEN) {
        //         console.log("Events Disconnected");
        //     }
        // }, false);
        // source.addEventListener('status', function(e) {
        //     console.log("status", e.data);
        //     statusCallback(e.data);
        //     if (window.pageState.reconnectTimer)
        //         clearTimeout(window.pageState.reconnectTimer);
        //     window.pageState.reconnectTimer = setTimeout(reconnect, 5000);
        // }, false);
    }
}

function showInElemByID(elemID, msg)
{
    const elem = document.getElementById(elemID);
    elem.innerHTML = msg;
}