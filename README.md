
## VIDI BalthasarLive-1.7.11 patch-set for CasparCG Server 2.0.7 Stable

Based on [CasparCG Server 2.0.7-Stable](https://github.com/CasparCG/Server/tree/2.0.7) by [SVT](http://svt.se/)

This patch-set adds some features that we believe to be critically important for building integrated automation solutions using CasparCG Server both as a Live IP-Steams Playout to SDI and a Video-Mixer/DVE Engine using customizable AMCP/HTML template.

This version of the patch-set is up & running on the [Life78](http://lifenews78.ru/) TV channel. We continue working on it for increasing performance of the live-streams output and conforming with CasparCG coding standards.

We hope to work it out together with CasparCG Development Team to merge or re-think these features into upcoming CasparCG v2.1.0 release.

### Features:
* 'mplayer_producer' for CasparCG 2.0.7. 'mplayer' has build-it features for IP-streams playout and gives the least of all open-source players and the most stable delay for IP-streams playout
* AMCP protocol additions for manipulation groups of DATA properties inside CasparCG
* introducing AMCP macro scripts – simple script format for batch AMCP execution
* production-ready templates and usage scenario for standard CUE/TAKE (Preview/Program) TV logic when controlling template execution scenario
* 'VIDIBUS' – RS485-to-AMCP bridge module for controlling several CasparCG servers connected to the common two-wire RS-485 bus using MODBUS-based protocol. This is suitable for sending short command like 'CUE' or 'TAKE' with a minimum reaction time and maximum reliability.

### Patch-set Authors:
* Yaroslav Polyakov ([@ypolyakov](https://github.com/ypolyakov))
* Iakov Pustilnik ([@yapus](https://github.com/yapus))
* Pavel Tolstov ([@ray66rus](https://github.com/ray66rus))
* Ivan Golubenko ([@FedorSymkin](https://github.com/FedorSymkin))
* Jura Boev ([@gonzo-coder](https://github.com/gonzo-coder))

### Acknowledgements
* [Anton Antipyev](https://www.linkedin.com/in/antonantipyev) – for the solution of the "different delays for different audio sources compensation" problem and other significant ideas about the product.

### License
CasparCG is distributed under the GNU General Public License GPLv3 or
higher, please see [LICENSE.TXT](/LICENSE.TXT) for details.

