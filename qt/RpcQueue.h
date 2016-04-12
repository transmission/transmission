/*
 * This file Copyright (C) Transmission authors and contributors
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#pragma once

#include <functional>
#include <type_traits>

#include <QFutureWatcher>
#include <QQueue>

#include "RpcClient.h"

/*
 * These overloads convert various forms of input closures to what we store internally.
 */
namespace detail
{
  /*
   * The response future -- the RPC engine returns one for each request made.
   */
  typedef QFuture<RpcResponse> RpcResponseFuture;

  /*
   * Internally queued function. Takes the last response future, makes a
   * request and returns a new response future.
   */
  typedef std::function<RpcResponseFuture (const RpcResponseFuture &)> QueuedFunction;

  /*
   * Internally stored error handler function. Takes the last response future and returns nothing.
   */
  typedef std::function<void (QFuture<RpcResponse>)> ErrorHandlerFunction;

  // normal closure, takes response and returns new future
  template <typename Func,
            typename std::enable_if<std::is_same<typename std::result_of<Func (const RpcResponse &)>::type, RpcResponseFuture>::value>::type * = nullptr>
  QueuedFunction normalizeFunc (const Func & func)
  {
    return [func] (const RpcResponseFuture & r) { return func (r.result ()); };
  }

  // closure without argument (first step), takes nothing and returns new future
  template <typename Func,
            typename std::enable_if<std::is_same<typename std::result_of<Func ()>::type, RpcResponseFuture>::value>::type * = nullptr>
  QueuedFunction normalizeFunc (const Func & func)
  {
    return [func] (const RpcResponseFuture &) { return func (); };
  }

  // closure without return value ("auxiliary"), takes response and returns nothing -- internally we reuse the last future
  template <typename Func,
            typename std::enable_if<std::is_same<typename std::result_of<Func (const RpcResponse &)>::type, void>::value>::type * = nullptr>
  QueuedFunction normalizeFunc (const Func & func)
  {
    return [func] (const RpcResponseFuture & r) { func (r.result ()); return r; };
  }

  // closure without argument and return value, takes nothing and returns nothing -- next function will also get nothing
  template <typename Func,
            typename std::enable_if<std::is_same<typename std::result_of<Func ()>::type, void>::value>::type * = nullptr>
  QueuedFunction normalizeFunc (const Func & func)
  {
    return [func] (const RpcResponseFuture & r) { func (); return r; };
  }

  // normal error handler, takes last response
  template <typename Func,
            typename std::enable_if<std::is_same<typename std::result_of<Func (const RpcResponse &)>::type, void>::value>::type * = nullptr>
  ErrorHandlerFunction normalizeErrorHandler (const Func & func)
  {
    return [func] (const RpcResponseFuture & r) { func (r.result ()); };
  }

  // error handler without an argument, takes nothing
  template <typename Func,
            typename std::enable_if<std::is_same<typename std::result_of<Func ()>::type, void>::value>::type * = nullptr>
  ErrorHandlerFunction normalizeErrorHandler (const Func & func)
  {
    return [func] (const RpcResponseFuture & r) { func (); };
  }
}

class RpcQueue: public QObject
{
  Q_OBJECT

public:
  explicit
  RpcQueue (QObject * parent = 0);

  void
  setTolerateErrors (bool tolerateErrors = true) { myTolerateErrors = tolerateErrors; }

  template <typename Func>
  void add (Func func)
  {
    myQueue.enqueue (std::make_pair (detail::normalizeFunc (func),
                                     detail::ErrorHandlerFunction ()));
  }

  template <typename Func, typename ErrorHandler>
  void add (Func func, ErrorHandler errorHandler)
  {
    myQueue.enqueue (std::make_pair (detail::normalizeFunc (func),
                                     detail::normalizeErrorHandler (errorHandler)));
  }

  QFuture<RpcResponse>
  future ();

  /*
   * The first function in queue is ran synchronously
   * (hence it may be e. g. a lambda capturing local variables by reference).
   */
  void
  run ();

private slots:
  void
  stepFinished ();

private:
  void runNext (const QFuture< RpcResponse > & response);

  bool myTolerateErrors;
  QFutureInterface<RpcResponse> myPromise;
  QQueue<std::pair<detail::QueuedFunction, detail::ErrorHandlerFunction>> myQueue;
  detail::ErrorHandlerFunction myNextErrorHandler;
  QFutureWatcher<RpcResponse> myFutureWatcher;
};
