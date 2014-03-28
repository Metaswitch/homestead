/**
 * @file httpstack_test.cpp UT for HttpStack module.
 *
 * Project Clearwater - IMS in the Cloud
 * Copyright (C) 2013  Metaswitch Networks Ltd
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version, along with the "Special Exception" for use of
 * the program along with SSL, set forth below. This program is distributed
 * in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details. You should have received a copy of the GNU General Public
 * License along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * The author can be reached by email at clearwater@metaswitch.com or by
 * post at Metaswitch Networks Ltd, 100 Church St, Enfield EN2 6BQ, UK
 *
 * Special Exception
 * Metaswitch Networks Ltd  grants you permission to copy, modify,
 * propagate, and distribute a work formed by combining OpenSSL with The
 * Software, or a work derivative of such a combination, even if such
 * copying, modification, propagation, or distribution would otherwise
 * violate the terms of the GPL. You must comply with the GPL in all
 * respects for all of the code used other than OpenSSL.
 * "OpenSSL" means OpenSSL toolkit software distributed by the OpenSSL
 * Project and licensed under the OpenSSL Licenses, or a work based on such
 * software and licensed under the OpenSSL Licenses.
 * "OpenSSL Licenses" means the OpenSSL License and Original SSLeay License
 * under which the OpenSSL Project distributes the OpenSSL toolkit software,
 * as those licenses appear in the file LICENSE-OPENSSL.
 */

#define GTEST_HAS_POSIX_RE 0
#include "test_utils.hpp"

#include "xmlutils.h"
#include "reg_state.h"

/// Fixture for HttpStackTest.
class XmlUtilsTest : public testing::Test
{
public:
  XmlUtilsTest()
  {
  }

  ~XmlUtilsTest()
  {
  }
};

TEST_F(XmlUtilsTest, SimpleMainline)
{
  std::string result = XmlUtils::build_ClearwaterRegData_xml(RegistrationState::REGISTERED, "<?xml?><IMSSubscription>test</IMSSubscription>");
  ASSERT_EQ("<ClearwaterRegData>\n\t<RegistrationState>REGISTERED</RegistrationState>\n\t<IMSSubscription>test</IMSSubscription>\n</ClearwaterRegData>\n\n", result);
}

TEST_F(XmlUtilsTest, GetPublicIds)
{
  std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?><IMSSubscription><PrivateID>rkdtestplan1@rkd.cw-ngv.com</PrivateID><ServiceProfile><PublicIdentity><Identity>sip:rkdtestplan1@rkd.cw-ngv.com</Identity><Extension><IdentityType>0</IdentityType></Extension></PublicIdentity><PublicIdentity><Identity>sip:rkdtestplan1_a@rkd.cw-ngv.com</Identity><Extension><IdentityType>0</IdentityType></Extension></PublicIdentity><PublicIdentity><Identity>sip:rkdtestplan1_b@rkd.cw-ngv.com</Identity><Extension><IdentityType>0</IdentityType></Extension></PublicIdentity><InitialFilterCriteria><Priority>0</Priority><TriggerPoint><ConditionTypeCNF>0</ConditionTypeCNF><SPT><ConditionNegated>0</ConditionNegated><Group>0</Group><Method>PUBLISH</Method><Extension></Extension></SPT><SPT><ConditionNegated>0</ConditionNegated><Group>0</Group><SIPHeader><Header>Event</Header><Content>.*presence.*</Content></SIPHeader><Extension></Extension></SPT><SPT><ConditionNegated>0</ConditionNegated><Group>0</Group><SessionCase>0</SessionCase><Extension></Extension></SPT><SPT><ConditionNegated>0</ConditionNegated><Group>1</Group><Method>PUBLISH</Method><Extension></Extension></SPT><SPT><ConditionNegated>0</ConditionNegated><Group>1</Group><SIPHeader><Header>Event</Header><Content>.*presence.*</Content></SIPHeader><Extension></Extension></SPT><SPT><ConditionNegated>0</ConditionNegated><Group>1</Group><SessionCase>3</SessionCase><Extension></Extension></SPT><SPT><ConditionNegated>0</ConditionNegated><Group>2</Group><Method>SUBSCRIBE</Method><Extension></Extension></SPT><SPT><ConditionNegated>0</ConditionNegated><Group>2</Group><SIPHeader><Header>Event</Header><Content>.*presence.*</Content></SIPHeader><Extension></Extension></SPT><SPT><ConditionNegated>0</ConditionNegated><Group>2</Group><SessionCase>1</SessionCase><Extension></Extension></SPT><SPT><ConditionNegated>0</ConditionNegated><Group>3</Group><Method>SUBSCRIBE</Method><Extension></Extension></SPT><SPT><ConditionNegated>0</ConditionNegated><Group>3</Group><SIPHeader><Header>Event</Header><Content>.*presence.*</Content></SIPHeader><Extension></Extension></SPT><SPT><ConditionNegated>0</ConditionNegated><Group>3</Group><SessionCase>2</SessionCase><Extension></Extension></SPT></TriggerPoint><ApplicationServer><ServerName>sip:127.0.0.1:5065</ServerName><DefaultHandling>0</DefaultHandling></ApplicationServer></InitialFilterCriteria></ServiceProfile></IMSSubscription>";

  std::vector<std::string> ids = XmlUtils::get_public_ids(xml);
  EXPECT_EQ(3u, ids.size());
}
