#include "PhrozenConnect.hpp"

#include <algorithm>
#include <sstream>
#include <exception>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/asio.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/nowide/convert.hpp>

#include <curl/curl.h>

#include <wx/progdlg.h>

#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/format.hpp"
#include "Http.hpp"
#include "libslic3r/AppConfig.hpp"
#include "Bonjour.hpp"
#include "slic3r/GUI/BonjourDialog.hpp"

namespace fs = boost::filesystem;
namespace pt = boost::property_tree;

namespace Slic3r {

PhrozenConnect::PhrozenConnect(DynamicPrintConfig *config) : OctoPrint( config )
{
    auto kHasPortInfo = m_host.find( ":8808" );
    if ( kHasPortInfo == std::string::npos ){
        m_host += ":8808";
    }
}

const char* PhrozenConnect::get_name() const { return "PhrozenConnect"; }

wxString PhrozenConnect::get_test_ok_msg() const
{
    return _(L("Connection to PhrozenConnect is working correctly."));
}

wxString PhrozenConnect::get_test_failed_msg(wxString& msg) const
{
    return GUI::from_u8((boost::format("%s: %s")
        % _utf8(L("Could not connect to PhrozenConnect"))
        % std::string(msg.ToUTF8())).str());
}

}