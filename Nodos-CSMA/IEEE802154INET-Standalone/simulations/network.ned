//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
//

package ieee802154.simulations;

import inet.world.radio.ChannelControl;
import ieee802154.src.NED.IEEE802154Host;

network net
{
    parameters:
        int numHosts=default (100);
        double nodeSeparation @unit("m")=default (50m);
        double constante @unit("m")=default (40m);
        double aux @unit("m")=default (0m);

        @display("is=vl");
    submodules:
        channelControl: ChannelControl {
            @display("p=50,50");//Indica la posición del chanelControl
        }
        IEEE802154Nodes[numHosts]: IEEE802154Host {
            //
 //            y =  (floor(index/sqrt(numFixHosts))*nodeSeparation)+(nodeSeparation);
   //          x = ((index%floor(sqrt(numFixHosts)))*nodeSeparation)+(nodeSeparation);
            
            //
            @display("i=old/x_blank;p=600,100;is=vs");//Indica la posición de los nodos
        }

    connections allowunconnected:
}
