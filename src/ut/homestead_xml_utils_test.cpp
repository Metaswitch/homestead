/**
 * @file xmlutils_test.cpp UT for XML utils.
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#define GTEST_HAS_POSIX_RE 0
#include "test_utils.hpp"

#include "homestead_xml_utils.h"
#include "reg_state.h"

/// Fixture for XmlUtilsTest.
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
  ChargingAddresses charging_addresses({"ccf1", "ccf2"}, {"ecf1", "ecf2"});
  std::string result;
  int rc = XmlUtils::build_ClearwaterRegData_xml(RegistrationState::REGISTERED,
                                                 "<?xml?><IMSSubscription>test</IMSSubscription>",
                                                 charging_addresses, 
                                                 result);

  ASSERT_EQ(200, rc);
  ASSERT_EQ("<ClearwaterRegData>\n\t<RegistrationState>REGISTERED</RegistrationState>\n\t<IMSSubscription>test</IMSSubscription>\n\t<ChargingAddresses>\n\t\t<CCF priority=\"1\">ccf1</CCF>\n\t\t<CCF priority=\"2\">ccf2</CCF>\n\t\t<ECF priority=\"1\">ecf1</ECF>\n\t\t<ECF priority=\"2\">ecf2</ECF>\n\t</ChargingAddresses>\n</ClearwaterRegData>\n\n", result);
}

TEST_F(XmlUtilsTest, Unregistered)
{
  ChargingAddresses charging_addresses;
  std::string result;
  int rc = XmlUtils::build_ClearwaterRegData_xml(RegistrationState::UNREGISTERED,
                                                 "<?xml?><IMSSubscription>test</IMSSubscription>",
                                                 charging_addresses,
                                                 result);
  ASSERT_EQ(200, rc);
  ASSERT_EQ("<ClearwaterRegData>\n\t<RegistrationState>UNREGISTERED</RegistrationState>\n\t<IMSSubscription>test</IMSSubscription>\n</ClearwaterRegData>\n\n", result);
}

TEST_F(XmlUtilsTest, InvalidRegState)
{
  ChargingAddresses charging_addresses;
  std::string result;
  int rc = XmlUtils::build_ClearwaterRegData_xml(RegistrationState::UNCHANGED,
                                                 "<?xml?><IMSSubscription>test</IMSSubscription>",
                                                 charging_addresses,
                                                 result);
  ASSERT_EQ(200, rc);
  ASSERT_EQ("<ClearwaterRegData>\n\t<RegistrationState>NOT_REGISTERED</RegistrationState>\n\t<IMSSubscription>test</IMSSubscription>\n</ClearwaterRegData>\n\n", result);
}

TEST_F(XmlUtilsTest, InvalidIMSSubscription)
{
  ChargingAddresses charging_addresses;
  std::string result;
  int rc = XmlUtils::build_ClearwaterRegData_xml(RegistrationState::REGISTERED,
                                                 "<?xml?><IMSSubscriptionwrong>test</IMSSubscriptionwrong>",
                                                 charging_addresses,
                                                 result);
  ASSERT_EQ(500, rc);
}

TEST_F(XmlUtilsTest, InvalidXML)
{
  ChargingAddresses charging_addresses;
  std::string result;
  int rc = XmlUtils::build_ClearwaterRegData_xml(RegistrationState::REGISTERED,
                                                 "<?xml?><InvalidXML</IMSSubscription>",
                                                 charging_addresses,
                                                 result);
  ASSERT_EQ(500, rc);
}

TEST_F(XmlUtilsTest, GetIds)
{
  std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?><IMSSubscription><PrivateID>rkdtestplan1@rkd.cw-ngv.com</PrivateID><ServiceProfile><PublicIdentity><Identity>sip:rkdtestplan1@rkd.cw-ngv.com</Identity><Extension><IdentityType>0</IdentityType></Extension></PublicIdentity><PublicIdentity><Identity>sip:rkdtestplan1_a@rkd.cw-ngv.com</Identity><Extension><IdentityType>0</IdentityType></Extension></PublicIdentity><PublicIdentity><Identity>sip:rkdtestplan1_b@rkd.cw-ngv.com</Identity><Extension><IdentityType>0</IdentityType></Extension></PublicIdentity><InitialFilterCriteria><Priority>0</Priority><TriggerPoint><ConditionTypeCNF>0</ConditionTypeCNF><SPT><ConditionNegated>0</ConditionNegated><Group>0</Group><Method>PUBLISH</Method><Extension></Extension></SPT><SPT><ConditionNegated>0</ConditionNegated><Group>0</Group><SIPHeader><Header>Event</Header><Content>.*presence.*</Content></SIPHeader><Extension></Extension></SPT><SPT><ConditionNegated>0</ConditionNegated><Group>0</Group><SessionCase>0</SessionCase><Extension></Extension></SPT><SPT><ConditionNegated>0</ConditionNegated><Group>1</Group><Method>PUBLISH</Method><Extension></Extension></SPT><SPT><ConditionNegated>0</ConditionNegated><Group>1</Group><SIPHeader><Header>Event</Header><Content>.*presence.*</Content></SIPHeader><Extension></Extension></SPT><SPT><ConditionNegated>0</ConditionNegated><Group>1</Group><SessionCase>3</SessionCase><Extension></Extension></SPT><SPT><ConditionNegated>0</ConditionNegated><Group>2</Group><Method>SUBSCRIBE</Method><Extension></Extension></SPT><SPT><ConditionNegated>0</ConditionNegated><Group>2</Group><SIPHeader><Header>Event</Header><Content>.*presence.*</Content></SIPHeader><Extension></Extension></SPT><SPT><ConditionNegated>0</ConditionNegated><Group>2</Group><SessionCase>1</SessionCase><Extension></Extension></SPT><SPT><ConditionNegated>0</ConditionNegated><Group>3</Group><Method>SUBSCRIBE</Method><Extension></Extension></SPT><SPT><ConditionNegated>0</ConditionNegated><Group>3</Group><SIPHeader><Header>Event</Header><Content>.*presence.*</Content></SIPHeader><Extension></Extension></SPT><SPT><ConditionNegated>0</ConditionNegated><Group>3</Group><SessionCase>2</SessionCase><Extension></Extension></SPT></TriggerPoint><ApplicationServer><ServerName>sip:127.0.0.1:5065</ServerName><DefaultHandling>0</DefaultHandling></ApplicationServer></InitialFilterCriteria></ServiceProfile></IMSSubscription>";

  std::vector<std::string> public_ids = XmlUtils::get_public_ids(xml);
  EXPECT_EQ(3u, public_ids.size());
  std::string private_id = XmlUtils::get_private_id(xml);
  EXPECT_EQ("rkdtestplan1@rkd.cw-ngv.com", private_id);
}

TEST_F(XmlUtilsTest, GetIdsInvalidXml)
{
  std::string xml = "?xml veron=\"1.0\" encoding=\"UTF-8\"?>";
  std::vector<std::string> public_ids = XmlUtils::get_public_ids(xml);
  EXPECT_EQ(0u, public_ids.size());
  std::string private_id = XmlUtils::get_private_id(xml);
  EXPECT_EQ("", private_id);
}

TEST_F(XmlUtilsTest, GetIdsMissingIds)
{
  std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?><IMSSubscription><NoPrivateID></NoPrivateID><ServiceProfile><PublicIdentity><Extension><IdentityType>0</IdentityType></Extension></PublicIdentity><PublicIdentity><Identity>sip:rkdtestplan1_a@rkd.cw-ngv.com</Identity><Extension><IdentityType>0</IdentityType></Extension></PublicIdentity><PublicIdentity><Identity>sip:rkdtestplan1_b@rkd.cw-ngv.com</Identity><Extension><IdentityType>0</IdentityType></Extension></PublicIdentity><InitialFilterCriteria><Priority>0</Priority><TriggerPoint><ConditionTypeCNF>0</ConditionTypeCNF><SPT><ConditionNegated>0</ConditionNegated><Group>0</Group><Method>PUBLISH</Method><Extension></Extension></SPT><SPT><ConditionNegated>0</ConditionNegated><Group>0</Group><SIPHeader><Header>Event</Header><Content>.*presence.*</Content></SIPHeader><Extension></Extension></SPT><SPT><ConditionNegated>0</ConditionNegated><Group>0</Group><SessionCase>0</SessionCase><Extension></Extension></SPT><SPT><ConditionNegated>0</ConditionNegated><Group>1</Group><Method>PUBLISH</Method><Extension></Extension></SPT><SPT><ConditionNegated>0</ConditionNegated><Group>1</Group><SIPHeader><Header>Event</Header><Content>.*presence.*</Content></SIPHeader><Extension></Extension></SPT><SPT><ConditionNegated>0</ConditionNegated><Group>1</Group><SessionCase>3</SessionCase><Extension></Extension></SPT><SPT><ConditionNegated>0</ConditionNegated><Group>2</Group><Method>SUBSCRIBE</Method><Extension></Extension></SPT><SPT><ConditionNegated>0</ConditionNegated><Group>2</Group><SIPHeader><Header>Event</Header><Content>.*presence.*</Content></SIPHeader><Extension></Extension></SPT><SPT><ConditionNegated>0</ConditionNegated><Group>2</Group><SessionCase>1</SessionCase><Extension></Extension></SPT><SPT><ConditionNegated>0</ConditionNegated><Group>3</Group><Method>SUBSCRIBE</Method><Extension></Extension></SPT><SPT><ConditionNegated>0</ConditionNegated><Group>3</Group><SIPHeader><Header>Event</Header><Content>.*presence.*</Content></SIPHeader><Extension></Extension></SPT><SPT><ConditionNegated>0</ConditionNegated><Group>3</Group><SessionCase>2</SessionCase><Extension></Extension></SPT></TriggerPoint><ApplicationServer><ServerName>sip:127.0.0.1:5065</ServerName><DefaultHandling>0</DefaultHandling></ApplicationServer></InitialFilterCriteria></ServiceProfile></IMSSubscription>";

  std::vector<std::string> public_ids = XmlUtils::get_public_ids(xml);
  EXPECT_EQ(2u, public_ids.size());
  std::string private_id = XmlUtils::get_private_id(xml);
  EXPECT_EQ("", private_id);
}
