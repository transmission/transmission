/**
 * @param {String} HTML representing any number of sibling elements
 * @return {NodeList}
 */
export function htmlToElements(html) {
    var template = document.createElement('template');
    template.innerHTML = html;
    return template.content;
}
