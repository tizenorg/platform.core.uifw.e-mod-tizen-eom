#ifndef _EOM_DOC_H_
#define _EOM_DOC_H_

/**
 * @mainpage EOM
 * @authors    SooChan Lim <sc1.lim@samsung.com>
 * @authors    JunKyeong Kim <jk0430.kim@samsung.com>
 * @authors    Gwanglim Lee <gl77.lee@samsung.com>
 * @authors    Roman Marchenko <r.marchenko@samsung.com>
 * @authors    Roman Peresipkyn <r.peresipkyn@samsung.com>
 *
 * @version    0.1.1
 * @par Introduction
 * EOM stends for External Output Manager. EOM is Enlightenment module designed
 * for managing external outputs. Lets clients showing its windows on external
 * outputs snd mirroring main output to external ones. Clients interract with
 * EOM by means of libeom.
 * \n
 * @par Architecture
 * EOM consists of eom/output/client/buffer objects.
 * eom is main object which holds all entire information about EOM in one place
 * (outputs and clients lists,info aboud main output etc.).
 * EOM creates output object for each external output it finds. It holds basic
 * information about an external output (resolution, name, states etc.). The main
 * purpose of output objects is configuration of external output and interaction
 * of EOM clients with an external output.
 * Each time a client is binded to EOM it creats client object. Only @b current
 * client can interract with an output and there can be only one @b current
 * client per output. Client become @b current if it successfuly set output's
 * attribute. When a client is sending buffers to be shown on external output
 * the client creates buffers and stores them in its buffer list. Tese buffers
 * will be shown on external outputs one by one.
 * \n
 * EOM module could be download from here.
 * @code
 $ git clone ssh://{user_id}@review.tizen.org:29418/platform/core/uifw/e-mod-tizen-eom
 * @endcode
 * Examples of EOM client-server interraction could be download from here
 * @code
 $ git clone ssh://{user_id}@review.tizen.org:29418/platform/core/uifw/ws-testcase
 * @endcode
 * Example related EOM loacated in ws-testcase/eo
 */

#endif /* _EOM_DOC_H_ */
