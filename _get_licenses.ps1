# Downloads the 19 canonical SPDX license texts into .\licenses\ as plain UTF-8.
# One-off helper for the New Project license feature. Safe to re-run.
$ErrorActionPreference = 'Stop'
$base = 'https://raw.githubusercontent.com/spdx/license-list-data/main/text'
$dir  = Join-Path $PSScriptRoot 'licenses'
New-Item -ItemType Directory -Force -Path $dir | Out-Null

# order: local key (also .rc/base name) , SPDX id , display name
$licenses = @(
  @('Unlicense'   ,'Unlicense'      ,'The Unlicense (Public Domain)'),
  @('MIT'         ,'MIT'            ,'MIT License'),
  @('Apache-2.0'  ,'Apache-2.0'     ,'Apache License 2.0'),
  @('GPL-2.0'     ,'GPL-2.0-only'   ,'GNU General Public License v2.0'),
  @('GPL-3.0'     ,'GPL-3.0-only'   ,'GNU General Public License v3.0'),
  @('LGPL-2.1'    ,'LGPL-2.1-only'  ,'GNU Lesser General Public License v2.1'),
  @('LGPL-3.0'    ,'LGPL-3.0-only'  ,'GNU Lesser General Public License v3.0'),
  @('AGPL-3.0'    ,'AGPL-3.0-only'  ,'GNU Affero General Public License v3.0'),
  @('BSD-2-Clause','BSD-2-Clause'   ,'BSD 2-Clause "Simplified" License'),
  @('BSD-3-Clause','BSD-3-Clause'   ,'BSD 3-Clause "New" License'),
  @('ISC'         ,'ISC'            ,'ISC License'),
  @('MPL-2.0'     ,'MPL-2.0'        ,'Mozilla Public License 2.0'),
  @('BSL-1.0'     ,'BSL-1.0'        ,'Boost Software License 1.0'),
  @('EUPL-1.2'    ,'EUPL-1.2'       ,'European Union Public Licence 1.2'),
  @('CC0-1.0'     ,'CC0-1.0'        ,'Creative Commons Zero v1.0 Universal'),
  @('CC-BY-4.0'   ,'CC-BY-4.0'      ,'Creative Commons Attribution 4.0'),
  @('CC-BY-SA-4.0','CC-BY-SA-4.0'   ,'Creative Commons Attribution-ShareAlike 4.0'),
  @('Artistic-2.0','Artistic-2.0'   ,'Artistic License 2.0'),
  @('WTFPL'       ,'WTFPL'          ,'Do What The F*ck You Want To Public License')
)

$fail = 0
foreach ($l in $licenses) {
  $key = $l[0]; $spdx = $l[1]
  $url = "$base/$spdx.txt"
  $out = Join-Path $dir "$key.txt"
  try {
    Invoke-WebRequest -Uri $url -OutFile $out -UseBasicParsing
    $len = (Get-Item $out).Length
    Write-Host ("OK   {0,-14} {1,7} bytes" -f $key, $len)
  } catch {
    Write-Host ("FAIL {0,-14} {1}" -f $key, $url) -ForegroundColor Red
    $fail++
  }
}
Write-Host ("`nDone. {0} file(s) failed." -f $fail)
