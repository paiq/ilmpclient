ilmpclient
==========

The ilmpclient library implements client functionality of the [ILMP specification](http://github.com/ImplicitLink/ilmpclient/blob/master/SPEC.md). It depends on [libboost](http://boost.org).

The library is header-only, and compilation should be rather straight forward. When linking, boost_system is required:

	g++ MyApp.cpp -Iilmpclient/ -lboost_system

### Example program ###
An complete example implementation is provided in the [notifier project](http://github.com/ImplicitLink/notifier).

### License ###
The program sources are released under the GNU General Public License.
