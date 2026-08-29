# Non-intrusive OpenOCD probe: telnet mdw on RUNNING target (no halt)
param([string[]]$Cmds = @("mdw 0xE000EDF0","mdw 0x20002138","mdw 0x200021a0"))
$c = New-Object System.Net.Sockets.TcpClient('localhost',4444)
$s = $c.GetStream()
$buf = New-Object System.Byte[] 4096
Start-Sleep -Milliseconds 300
while ($s.DataAvailable) { [void]$s.Read($buf,0,$buf.Length) }  # drain banner
$enc = [System.Text.Encoding]::ASCII
$bytes = $enc.GetBytes(($Cmds -join "`r`n") + "`r`n")
$s.Write($bytes,0,$bytes.Length); $s.Flush()
Start-Sleep -Milliseconds 600
$out = ""
while ($s.DataAvailable) {
  $n = $s.Read($buf,0,$buf.Length)
  $out += $enc.GetString($buf,0,$n)
}
$c.Close()
Write-Output "RAW:[$out]"
