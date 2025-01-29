async function loadContent() {
    document.getElementById("UserCfg").className = "active"
    const response = await fetch('/ap_usr.html');
    document.getElementById('content').innerHTML = await response.text();

    function save() {
        let n = document.getElementById('unit_name').value;
        fetch('/usercfg', {method: 'POST', body: JSON.stringify({unit_name: n})})
            .then(r => r.json()).then(d => alert(d.m))
    }

    document.addEventListener('click', event => {
        if (event.target.id === 'saveBtn') save();
    });
}