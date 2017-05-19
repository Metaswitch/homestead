/**
 * @file cassandracache.h class definition of a cassandra-backed cache.
 *
 * Copyright (C) Metaswitch Networks
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include <cache.h>
#include <authvector.h>
#include <unistd.h>
#include <semaphore.h>
#include <algorithm>
#include <iterator>

sem_t sem;

using namespace std;

class ExampleTransaction : public Cache::Transaction
{
public:
  ExampleTransaction(Cache::Request* req) :
    Cache::Transaction(req)
  {}

  void on_success()
  {
    cout << "Request succeeded" << endl;

    if (typeid(*_req) == typeid(Cache::GetIMSSubscription))
    {
      std::string xml;
      dynamic_cast<Cache::GetIMSSubscription*>(_req)->get_result(xml);

      cout << "  XML:" << xml << endl;
    }
    else if (typeid(*_req) == typeid(Cache::GetAuthVector))
    {
      DigestAuthVector av;
      dynamic_cast<Cache::GetAuthVector*>(_req)->get_result(av);

      cout << "  digest_ha1: " << av.ha1 << "\n  realm: " << av.realm <<
        "\n  qop: " << av.qop << "\n  preferred: " << av.preferred << endl;
    }
    else if (typeid(*_req) == typeid(Cache::GetAssociatedPublicIDs))
    {
      std::vector<std::string> ids;
      dynamic_cast<Cache::GetAssociatedPublicIDs*>(_req)->get_result(ids);

      cout << "IDs:";
      for (vector<string>::iterator it = ids.begin(); it != ids.end(); ++it) {
        cout << "\n    " << *it;
      }
      cout << endl;
    }

    sem_post(&sem);
  }

  void on_failure(Cache::ResultCode rc, std::string& text)
  {
    cout << "Request failed\n  Result: " << rc <<"\n  Text: " << text << endl;
    sem_post(&sem);
  }
};


int main(int argc, char *argv[])
{
  sem_init(&sem, 0, 0);
  Cache::Request* req;
  Cache::Transaction *trx;
  DigestAuthVector av;

  Cache* cache = Cache::get_instance();

  std::string alice_pub = "sip:alice@example.com";
  std::string alice_pub2 = "sip:bob@example.com";
  std::string alice_priv = "alice@example.com";
  std::string alice_ims_sub_xml = "Alice IMS subscription XML body";

  cout << "------------ Startup ---------------" << endl;
  cache->initialize();
  cache->configure("localhost", 9160, 1);
  Cache::ResultCode rc = cache->start();
  cout << "Start return code is " << rc << endl;
  cout << "------------ Done ---------------" << endl << endl;


  cout << "------------ Get IMS sub (not present) ---------------" << endl;
  req = new Cache::GetIMSSubscription(alice_pub);
  trx = new ExampleTransaction(req);
  cache->send(trx); trx = NULL; req = NULL;
  sem_wait(&sem);
  cout << "------------ Done ---------------" << endl << endl;


  cout << "------------ Put IMS sub ---------------" << endl;
  req = new Cache::PutIMSSubscription(alice_pub, alice_ims_sub_xml, Cache::generate_timestamp());
  trx = new ExampleTransaction(req);
  cache->send(trx); req = NULL; trx = NULL;
  sem_wait(&sem);
  cout << "------------ Done ---------------" << endl << endl;


  cout << "------------ Get IMS sub (present) ---------------" << endl;
  req = new Cache::GetIMSSubscription(alice_pub);
  trx = new ExampleTransaction(req);
  cache->send(trx); req = NULL; trx = NULL;
  sem_wait(&sem);
  cout << "------------ Done ---------------" << endl << endl;


  cout << "------------ Delete public ID ---------------" << endl;
  req = new Cache::DeletePublicIDs(alice_pub, Cache::generate_timestamp());
  trx = new ExampleTransaction(req);
  cache->send(trx); req = NULL; trx = NULL;
  sem_wait(&sem);

  cout << "Check " << alice_pub << " is no longer present" << endl;
  req = new Cache::GetIMSSubscription(alice_pub);
  trx = new ExampleTransaction(req);
  cache->send(trx); req = NULL; trx = NULL;
  sem_wait(&sem);
  cout << "------------ Done ---------------" << endl << endl;


  cout << "------------ Get AV (not present) ---------------" << endl;
  req = new Cache::GetAuthVector(alice_priv);
  trx = new ExampleTransaction(req);
  cache->send(trx); req = NULL; trx = NULL;
  sem_wait(&sem);
  cout << "------------ Done ---------------" << endl << endl;


  cout << "------------ Put AV ---------------" << endl;
  av.ha1 = "Some-hash";
  av.realm = "example.com";
  av.qop = "auth";
  av.preferred = true;
  req = new Cache::PutAuthVector(alice_priv, av, Cache::generate_timestamp());
  trx = new ExampleTransaction(req);
  cache->send(trx); req = NULL; trx = NULL;
  sem_wait(&sem);
  cout << "------------ Done ---------------" << endl << endl;


  cout << "------------ Get AV (present) ---------------" << endl;
  req = new Cache::GetAuthVector(alice_priv);
  trx = new ExampleTransaction(req);
  cache->send(trx); req = NULL; trx = NULL;
  sem_wait(&sem);
  cout << "------------ Done ---------------" << endl << endl;


  cout << "------------ Get AV (no assoc public ID) ---------------" << endl;
  req = new Cache::GetAuthVector(alice_priv, alice_pub);
  trx = new ExampleTransaction(req);
  cache->send(trx); req = NULL; trx = NULL;
  sem_wait(&sem);
  cout << "------------ Done ---------------" << endl << endl;


  cout << "------------ Associate some public IDs ---------------" << endl;
  req = new Cache::PutAssociatedPublicID(alice_priv, alice_pub, Cache::generate_timestamp());
  trx = new ExampleTransaction(req);
  cache->send(trx); req = NULL; trx = NULL;
  sem_wait(&sem);
  req = new Cache::PutAssociatedPublicID(alice_priv, alice_pub2, Cache::generate_timestamp());
  trx = new ExampleTransaction(req);
  cache->send(trx); req = NULL; trx = NULL;
  sem_wait(&sem);
  cout << "------------ Done ---------------" << endl << endl;


  cout << "------------ Get assoc public IDs ---------------" << endl;
  req = new Cache::GetAssociatedPublicIDs(alice_priv);
  trx = new ExampleTransaction(req);
  cache->send(trx); req = NULL; trx = NULL;
  sem_wait(&sem);
  cout << "------------ Done ---------------" << endl << endl;


  cout << "------------ Get AV (with assoc public ID) ---------------" << endl;
  req = new Cache::GetAuthVector(alice_priv, alice_pub);
  trx = new ExampleTransaction(req);
  cache->send(trx); req = NULL; trx = NULL;
  sem_wait(&sem);
  cout << "------------ Done ---------------" << endl << endl;


  cout << "------------ Delete private ID ---------------" << endl;
  req = new Cache::DeletePrivateIDs(alice_priv, Cache::generate_timestamp());
  trx = new ExampleTransaction(req);
  cache->send(trx); req = NULL; trx = NULL;
  sem_wait(&sem);


  cout << "Check " << alice_priv << " is no longer present" << endl;
  req = new Cache::GetIMSSubscription(alice_priv);
  trx = new ExampleTransaction(req);
  cache->send(trx); req = NULL; trx = NULL;
  sem_wait(&sem);
  cout << "------------ Done ---------------" << endl << endl;


  cout << "------------ Stopping ---------------" << endl;
  cache->stop();
  cache->wait_stopped();
  cout << "Stopped OK" << endl;
  cout << "------------ Done ---------------" << endl << endl;

  return 0;
}
