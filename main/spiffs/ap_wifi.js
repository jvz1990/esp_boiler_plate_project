async function loadContent() {
    document.getElementById("WiFi").className = "active"
    const response = await fetch('/ap_wifi.html');
    document.getElementById('content').innerHTML = await response.text();

    function addNetwork() {
        let n = document.createElement('div');
        n.className = 'network';
        n.innerHTML = `<label><input name='ssid' placeholder='Wi-Fi Network' required></label>
                       <label><input type='password' name='pass' placeholder='Password' required></label>`;
        document.getElementById('networks').appendChild(n)
    }

    function save() {
        const networks = Array.from(document.querySelectorAll('.network')).map(e => ({
            ssid: e.querySelector('input[name=ssid]').value,
            pass: e.querySelector('input[name=pass]').value
        }));
        fetch('/wifi', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({networks})
        })
            .then(r => r.json())
            .then(d => alert(d.m))
            .catch(e => alert('Error: ' + e))
    }

    document.addEventListener('click', event => {
        if (event.target.id === 'addNetworkBtn') addNetwork();
        if (event.target.id === 'saveBtn') save();
    });
}