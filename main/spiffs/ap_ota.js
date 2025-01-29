async function loadContent() {
    document.getElementById("OTA").className = "active"
    const response = await fetch('/ap_ota.html');
    document.getElementById('content').innerHTML = await response.text();

    function save() {
        let o = document.getElementById('ota_url').value,
            v = document.getElementById('version_url').value;
        fetch('/ota', {method: 'POST', body: JSON.stringify({ota_url: o, version_url: v})})
            .then(r => r.json()).then(d => alert(d.m))
    }

    document.addEventListener('click', event => {
        if (event.target.id === 'saveBtn') save();
    });
}