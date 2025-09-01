#ifndef slic3r_PhrozenConnect_hpp_
#define slic3r_PhrozenConnect_hpp_

#include "OctoPrint.hpp"
namespace Slic3r 
{

class PhrozenConnect : public OctoPrint
{
public:
	explicit PhrozenConnect(DynamicPrintConfig* config);
	~PhrozenConnect() override = default;
    const char* get_name() const override;
    wxString get_test_ok_msg() const override;
    wxString get_test_failed_msg(wxString& msg) const override;
};

}

#endif