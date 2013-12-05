#include <string>

class DiameterStack
{
public:
  class Exception
  {
  public:
    inline Exception(const char* func, int rc) : _func(func), _rc(rc) {};
    const char* _func;
    const int _rc;
  };

  static inline DiameterStack* getInstance() {return INSTANCE;};
  void initialize();
  void configure(std::string filename);
  void start();
  void stop();
  void wait_stopped();

private:
  static DiameterStack* INSTANCE;
  static DiameterStack DEFAULT_INSTANCE;

  DiameterStack();

  // Don't implement the following, to avoid copies of this instance.
  DiameterStack(DiameterStack const&);
  void operator=(DiameterStack const&);

  bool _initialized;
};
