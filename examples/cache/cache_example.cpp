/**
 * @file cassandracache.h class definition of a cassandra-backed cache.
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

#include <cache.h>
#include <authvector.h>
#include <unistd.h>
#include <semaphore.h>
#include <algorithm>
#include <iterator>

sem_t sem;

using namespace std;

template <typename T>
class ExampleRequest : public T
{
public:
  ExampleRequest(std::string& arg1) : T(arg1) {}
  ExampleRequest(string& arg1, string& arg2, int64_t arg3) : T(arg1, arg2, arg3) {}
  ExampleRequest(string& arg1, int64_t arg2) : T(arg1, arg2) {}
  ExampleRequest(string& arg1, DigestAuthVector& arg2, int64_t arg3) : T(arg1, arg2, arg3) {}
  ExampleRequest(string& arg1, string& arg2) : T(arg1, arg2) {}

  virtual ~ExampleRequest() {}

  void on_failure(Cache::ResultCode rc, std::string& text)
  {
    cout << "Request failed\n  Result: " << rc <<"\n  Text: " << text << endl;
    sem_post(&sem);
  }

  void on_success(std::string& xml)
  {
    cout << "Request succeeded:\n  XML:" << xml << endl;
    sem_post(&sem);
  }

  void on_success()
  {
    cout << "Request succeeded" << endl;
    sem_post(&sem);
  }

  void on_success(DigestAuthVector& av)
  {
    cout << "Request suceeded:\n  digest_ha1: " << av.ha1 << "\n  realm: " << av.realm << 
            "\n  qop: " << av.qop << "\n  preferred: " << av.preferred << endl;
    sem_post(&sem);
  }

  void on_success(vector<string>& vs)
  {
    cout << "Request succeeded:";
    for (vector<string>::iterator it = vs.begin(); it != vs.end(); ++it) {
      cout << "\n  " << *it;
    } 
    cout << endl;
    sem_post(&sem);
  }

private:
  pthread_mutex_t _lock;
};


int main(int argc, char *argv[])
{
  sem_init(&sem, 0, 0);
  Cache::Request* req;
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
  req = new ExampleRequest<Cache::GetIMSSubscription>(alice_pub);
  cache->send(req); req = NULL;
  sem_wait(&sem);
  cout << "------------ Done ---------------" << endl << endl;

  cout << "------------ Put IMS sub ---------------" << endl;
  req = new ExampleRequest<Cache::PutIMSSubscription>(alice_pub, alice_ims_sub_xml, Cache::generate_timestamp());
  cache->send(req); req = NULL;
  sem_wait(&sem);
  cout << "------------ Done ---------------" << endl << endl;

  cout << "------------ Get IMS sub (present) ---------------" << endl;
  req = new ExampleRequest<Cache::GetIMSSubscription>(alice_pub);
  cache->send(req); req = NULL;
  sem_wait(&sem);
  cout << "------------ Done ---------------" << endl << endl;

  cout << "------------ Delete public ID ---------------" << endl;
  req = new ExampleRequest<Cache::DeletePublicIDs>(alice_pub, Cache::generate_timestamp());
  cache->send(req); req = NULL;
  sem_wait(&sem);

  cout << "Check " << alice_pub << " is no longer present" << endl;
  req = new ExampleRequest<Cache::GetIMSSubscription>(alice_pub);
  cache->send(req); req = NULL;
  sem_wait(&sem);
  cout << "------------ Done ---------------" << endl << endl;

  cout << "------------ Get AV (not present) ---------------" << endl;
  req = new ExampleRequest<Cache::GetAuthVector>(alice_priv);
  cache->send(req); req = NULL;
  sem_wait(&sem);
  cout << "------------ Done ---------------" << endl << endl;

  cout << "------------ Put AV ---------------" << endl;
  av.ha1 = "Some-hash";
  av.realm = "example.com";
  av.qop = "auth";
  av.preferred = true;
  req = new ExampleRequest<Cache::PutAuthVector>(alice_priv, av, Cache::generate_timestamp());
  cache->send(req); req = NULL;
  sem_wait(&sem);
  cout << "------------ Done ---------------" << endl << endl;

  cout << "------------ Get AV (present) ---------------" << endl;
  req = new ExampleRequest<Cache::GetAuthVector>(alice_priv);
  cache->send(req); req = NULL;
  sem_wait(&sem);
  cout << "------------ Done ---------------" << endl << endl;

  cout << "------------ Get AV (no assoc public ID) ---------------" << endl;
  req = new ExampleRequest<Cache::GetAuthVector>(alice_priv, alice_pub);
  cache->send(req); req = NULL;
  sem_wait(&sem);
  cout << "------------ Done ---------------" << endl << endl;

  cout << "------------ Associate some public IDs ---------------" << endl;
  req = new ExampleRequest<Cache::PutAssociatedPublicID>(alice_priv, alice_pub, Cache::generate_timestamp());
  cache->send(req); req = NULL;
  sem_wait(&sem);
  req = new ExampleRequest<Cache::PutAssociatedPublicID>(alice_priv, alice_pub2, Cache::generate_timestamp());
  cache->send(req); req = NULL;
  sem_wait(&sem);
  cout << "------------ Done ---------------" << endl << endl;

  cout << "------------ Get assoc public IDs ---------------" << endl;
  req = new ExampleRequest<Cache::GetAssociatedPublicIDs>(alice_priv);
  cache->send(req); req = NULL;
  sem_wait(&sem);
  cout << "------------ Done ---------------" << endl << endl;

  cout << "------------ Get AV (with assoc public ID) ---------------" << endl;
  req = new ExampleRequest<Cache::GetAuthVector>(alice_priv, alice_pub);
  cache->send(req); req = NULL;
  sem_wait(&sem);
  cout << "------------ Done ---------------" << endl << endl;

  cout << "------------ Delete private ID ---------------" << endl;
  req = new ExampleRequest<Cache::DeletePrivateIDs>(alice_priv, Cache::generate_timestamp());
  cache->send(req); req = NULL;
  sem_wait(&sem);

  cout << "Check " << alice_priv << " is no longer present" << endl;
  req = new ExampleRequest<Cache::GetIMSSubscription>(alice_priv);
  cache->send(req); req = NULL;
  sem_wait(&sem);
  cout << "------------ Done ---------------" << endl << endl;

  cout << "------------ Stopping ---------------" << endl;
  cache->stop();
  cache->wait_stopped();
  cout << "Stopped OK" << endl;
  cout << "------------ Done ---------------" << endl << endl;

  return 0;
}
