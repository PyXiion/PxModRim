.pragma library

// Steam's image CDN resizes on request. Asking for the size that is shown keeps
// both the download and the decoded pixmap small.
function sized(url, width, height) {
    if (!url) return ""
    if (!/^https:\/\/[^/]*(steamusercontent\.com|steamstatic\.com)\//.test(url)) return url
    var separator = url.indexOf("?") < 0 ? "?" : "&"
    return url + separator + "imw=" + width + "&imh=" + height + "&ima=fit&impolicy=Letterbox"
}

// Round up to a step so that small layout changes do not reload the image.
function bucket(value, step) {
    return Math.max(step, Math.ceil(value / step) * step)
}
