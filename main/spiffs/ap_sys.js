async function loadContent() {
    document.getElementById("System").className = "active"
    const response = await fetch('/ap_sys.html');
    document.getElementById('content').innerHTML = await response.text();

    function save() {
        let n = document.getElementById('logLevel').value;
        fetch('/system', {method: 'POST', body: JSON.stringify({logLevel: n})})
            .then(r => r.json()).then(d => alert(d.m))
    }

    document.addEventListener('click', event => {
        if (event.target.id === 'saveBtn') save();
    });
}