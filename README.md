## VIDI MelchiorVS-1.2.0 patch-set for CasparCG Server 2.0.7 Stable

Based on [CasparCG Server 2.0.7-Stable](https://github.com/CasparCG/Server/tree/2.0.7) by [SVT](http://svt.se/)

This patch-set adds some features that we believe to be critically important for building integrated automation solutions using CasparCG Server both as a Television Studio Playout Server and On-Air Graphics Playout Server for News TV Channels.

We hope to work it out together with CasparCG Development Team to merge or re-think these features into upcoming CasparCG v2.1.0 release.

### Features:
* frame accurate SEEK command within h264 GOP
* 'CINFONE' AMCP command used to query single file status without scanning media/ directory
* 'INFO LAYERS' command used to query data for the a list by one AMCP command
* 'DIRECTPATH' parameter used for playing files from more than one media/ directory

### Patch-set Authors:
* Yaroslav Polyakov ([@ypolyakov](https://github.com/ypolyakov))
* Iakov Pustilnik ([@yapus](https://github.com/yapus))
* Pavel Tolstov ([@ray66rus](https://github.com/ray66rus))
* Ivan Golubenko ([@FedorSymkin](https://github.com/FedorSymkin))

CasparCG is distributed under the GNU General Public License GPLv3 or
higher, please see [LICENSE.TXT](/LICENSE.TXT) for details.

