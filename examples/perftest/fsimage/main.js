
function bodyIsLoaded() {

    // Setup page state
    document.getElementById("webSocketState").innerHTML = "WebSocket is not connected";

    // Connect ws
    connectWs();    
}

function connectWs()
{
    // Setup webSocket
    if (!window.webSocket) 
    {
        console.log('Attempting WebSocket connection');
        window.webSocket = new WebSocket('ws://' + location.hostname + '/ws');
        window.webSocket.binaryType = "arraybuffer";

        // Websocket opened
        window.webSocket.onopen = function(evt) 
        {
            console.log('WebSocket connection opened');
            // window.webSocket.send("It's open! Hooray!!!");
            document.getElementById("webSocketState").innerHTML = "WebSocket is connected!";

            // Set a timer to send a message every 5 seconds
            if (!window.pageState)
                window.pageState = {};
            window.pageState.wsMsgCounter = 1;
            window.pageState.msgTimer = setInterval(() => {
                window.webSocket.send("Hello from the browser! " + (window.pageState.wsMsgCounter++) + "\n")
            }, 5000);

        }
        
        // Websocket message received
        window.webSocket.onmessage = function(evt) 
        {
            const msg = new Uint8Array(evt.data);
            console.log("WebSocket rx ", msg);
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
        window.webSocket.onclose = function(evt) 
        {
            clearInterval(window.pageState.msgTimer);
            console.log('Websocket connection closed');
            document.getElementById("webSocketState").innerHTML = "WebSocket closed";
            window.webSocket = null;
            if (window.pageState.reconnectTimer)
                clearTimeout(window.pageState.reconnectTimer);
            window.pageState.reconnectTimer = setTimeout(reconnect, 5000);
        }
        
        // Websocket error
        window.webSocket.onerror = function(evt) 
        {
            console.log('Websocket error: ' + evt);
            document.getElementById("webSocketState").innerHTML = "WebSocket error";
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

// Attempt to reconnect websocket
function reconnect(event)
{
    connectWs();
}