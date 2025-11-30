# Stop any python processes running idf.py monitor
$ps = Get-Process -Name python -ErrorAction SilentlyContinue
if ($ps) {
  foreach ($p in $ps) {
    $winproc = Get-CimInstance Win32_Process -Filter ("ProcessId={0}" -f $p.Id)
    $cmd = $winproc.CommandLine
    if ($cmd -and $cmd -match 'idf.py') {
      Write-Host "Stopping python process PID=$($p.Id) CMD=$cmd"
      Stop-Process -Id $p.Id -Force
    }
  }
} else {
  Write-Host "No python processes found"
}
